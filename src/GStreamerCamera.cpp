#include "GStreamerCamera.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <string.h> // For memcpy
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

using namespace godot;

void GStreamerCamera::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("is_streaming_active"), &GStreamerCamera::is_streaming_active);


    ClassDB::bind_method(D_METHOD("get_camera_name"), &GStreamerCamera::get_device_name);
	ClassDB::bind_method(D_METHOD("set_camera_name", "cameraName"), &GStreamerCamera::set_device_name);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "deviceName", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT),"set_camera_name","get_camera_name");


    ClassDB::bind_method(D_METHOD("get_frame_size"), &GStreamerCamera::get_frame_size);
	ClassDB::bind_method(D_METHOD("set_frame_size", "size"), &GStreamerCamera::set_frame_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "frameSize", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT),"set_frame_size","get_frame_size");

}

void GStreamerCamera::set_device_name(const String cameraName)
{
    this->deviceName = cameraName;
    this->devicePath = vformat("./%s", this->deviceName);

} 
String GStreamerCamera::get_device_name() const
{
    return this->deviceName;
}

void GStreamerCamera::set_frame_size(const Vector2i size)
{
    this->frameSize = size;
    this->viewport->set_size(this->frameSize);
} 
Vector2i GStreamerCamera::get_frame_size() const
{
    return this->frameSize;
}

GStreamerCamera::GStreamerCamera()
{

    this->camera = memnew(Camera3D);
    this->camera->set_fov(90);

    this->viewport = memnew(SubViewport);
    this->viewport->add_child(this->camera);
    this->viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    this->viewport->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);

    this->add_child(this->viewport);
    
    this->pts = 0;



}

GStreamerCamera::~GStreamerCamera()
{
    stop_stream();
}

void GStreamerCamera::_process(double delta) {
    this->send_frame(this->viewport->get_texture().ptr()->get_image().ptr()->get_data());
}

void GStreamerCamera::_ready()
{
    this->initializeGStreamer();
}

void GStreamerCamera::initializeGStreamer()
{
    if (this->is_streaming)
    {
        UtilityFunctions::printerr("Stream is already active.");
        return;
    }

    
    

    if (std::remove(this->devicePath.utf8().get_data()) != 0)
    {
    }

    if (!gst_is_initialized())
    {
        GError *error = nullptr;
        if (!gst_init_check(nullptr, nullptr, &error))
        {
            UtilityFunctions::printerr(vformat("GStreamer failed to initialize: %s", error->message));
            g_clear_error(&error);
            return;
        }
    }

    const char *NO_BUFFER_QUEUE = "queue max-size-bytes=0 max-size-buffers=0 max-size-time=1 ! ";

    if (this->pipelineString == "")
    {
        this->pipelineString = vformat(
            "appsrc name=source is-live=true format=time ! " +
                String(NO_BUFFER_QUEUE) +
                "capsfilter caps=video/x-raw,format=RGB,width=%d,height=%d,framerate=30/1 ! " +
                "videoconvert ! " +
                String(NO_BUFFER_QUEUE) +
                // Output to a raw, uncompressed format like I420 for common SHM transport
                "video/x-raw,format=I420 ! " +
                // shmsink: Writes to a shared memory segment managed by the 'socket-path'
                "shmsink socket-path=%s wait-for-connection=false sync=false qos=false",
            this->frameSize.x, this->frameSize.y, this->devicePath);
    }

    UtilityFunctions::print(vformat("GStreamer Pipeline: %s", this->pipelineString));

    GError *error = nullptr;
    pipeline = gst_parse_launch(this->pipelineString.utf8().get_data(), &error);

    if (!pipeline)
    {
        UtilityFunctions::printerr(vformat("Failed to create pipeline: %s", error->message));
        g_clear_error(&error);
        return;
    }

    GstElement *source_element = gst_bin_get_by_name(GST_BIN(pipeline), "source");
    appsrc = GST_APP_SRC(source_element);

    if (!appsrc)
    {
        UtilityFunctions::printerr("Failed to get appsrc element. Pipeline cleanup needed.");
        gst_object_unref(pipeline);
        pipeline = nullptr;
        return;
    }

    g_object_set(G_OBJECT(appsrc), "max-buffers", (guint)1, "block", false, NULL); // Or even 1

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    this->is_streaming = true;
    UtilityFunctions::print(vformat("GStreamer pipeline started: %dx%d to %s", this->frameSize.x, this->frameSize.y, this->devicePath));
}

void GStreamerCamera::send_frame(const PackedByteArray &pixel_data)
{

    if (!is_streaming || !appsrc)
    {
        return;
    }

    // Expected size: Width * Height * 3 bytes/pixel (for RGB8)
    gsize expected_size = (gsize)this->frameSize.x * this->frameSize.y * 3;
    if (pixel_data.size() != expected_size)
    {
        UtilityFunctions::printerr(vformat("Received data size mismatch. Expected %d bytes, got %d. Check Image.FORMAT_RGB8 conversion.", expected_size, pixel_data.size()));
        return;
    }

    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, pixel_data.size(), nullptr);
    if (!buffer)
    {
        UtilityFunctions::printerr("Failed to allocate GStreamer buffer.");
        return;
    }

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE))
    {
        const uint8_t *raw_data = pixel_data.ptr();
        memcpy(map.data, raw_data, pixel_data.size());
        gst_buffer_unmap(buffer, &map);
    }
    else
    {
        UtilityFunctions::printerr("Failed to map GStreamer buffer memory.");
        gst_buffer_unref(buffer);
        return;
    }

    // 30 FPS = 1/30 second duration = 33,333,333 nanoseconds
    const guint64 FRAME_DURATION = 33333333;

    GST_BUFFER_PTS(buffer) = pts;
    GST_BUFFER_DURATION(buffer) = FRAME_DURATION;
    pts += FRAME_DURATION;

    GstFlowReturn ret = gst_app_src_push_buffer(appsrc, buffer);

    if (ret != GST_FLOW_OK)
    {
        UtilityFunctions::printerr(vformat("Error pushing buffer to appsrc: %d", ret));
    }
}

void GStreamerCamera::stop_stream()
{

    if (pipeline)
    {
        // Send EOS (End-Of-Stream) to cleanly shut down the pipeline
        if (appsrc)
        {
            gst_app_src_end_of_stream(appsrc);
        }

        // Stop and clean up the pipeline
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
        appsrc = nullptr;
        is_streaming = false;
        UtilityFunctions::print("GStreamer pipeline stopped.");
    }
    if (std::remove(this->devicePath.utf8().get_data()) != 0)
    {
        // Error handling if the file couldn't be deleted, perhaps it was already gone.
    }
}
