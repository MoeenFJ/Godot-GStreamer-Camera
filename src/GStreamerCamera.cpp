#include "GStreamerCamera.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/callable.hpp>
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

    ClassDB::bind_method(D_METHOD("get_device_path"), &GStreamerCamera::get_device_path);
    ClassDB::bind_method(D_METHOD("set_device_path", "devicePath"), &GStreamerCamera::set_device_path);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "devicePath", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT), "set_device_path", "get_device_path");

    ClassDB::bind_method(D_METHOD("get_frame_size"), &GStreamerCamera::get_frame_size);
    ClassDB::bind_method(D_METHOD("set_frame_size", "size"), &GStreamerCamera::set_frame_size);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "frameSize", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT), "set_frame_size", "get_frame_size");

    ClassDB::bind_method(D_METHOD("get_pipeline_string"), &GStreamerCamera::get_pipeline_string);
    ClassDB::bind_method(D_METHOD("set_pipeline_string", "pipelineString"), &GStreamerCamera::set_pipeline_string);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "GStreamer Pipeline String", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT), "set_pipeline_string", "get_pipeline_string");
}


void GStreamerCamera::set_pipeline_string(const String pipelineString)
{
    this->pipelineString = pipelineString;
}
String GStreamerCamera::get_pipeline_string() const
{
    return this->pipelineString;
}


void GStreamerCamera::set_device_path(const String devicePath)
{
    this->devicePath = devicePath;
}
String GStreamerCamera::get_device_path() const
{
    return this->devicePath;
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

    this-> transformNode = memnew(Node3D);
    this->transformNode->set_name("GStreamerCameraTransformNode");

    this->viewport = memnew(SubViewport);
    this->viewport->set_size(this->frameSize);
    this->viewport->set_name("GStreamerCameraViewport");
    this->viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
    this->viewport->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);

    this->pts = 0;
}

GStreamerCamera::~GStreamerCamera()
{
    stop_stream();
}

bool doneonce = false;
void GStreamerCamera::_process(double delta)
{


    if(!this->transformNode->is_inside_tree())
        this->add_sibling(this->transformNode);
    if(!this->viewport->is_inside_tree())
        this->add_sibling(this->viewport);
    if (!doneonce && this->transformNode->is_inside_tree() && this->viewport->is_inside_tree())
    {
        this->transformNode->set_global_transform(this->get_global_transform());
        this->reparent(this->viewport);
        this->set_current(true);
        doneonce = true;
    }
    if(doneonce)
        this->set_global_transform(this->transformNode->get_global_transform());
    this->send_frame();
}

void GStreamerCamera::_ready()
{
    this->imageFormat = this->viewport->get_texture().ptr()->get_image().ptr()->get_format();
    this->set_current(false);
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

void GStreamerCamera::send_frame()
{

    PackedByteArray pixel_data;

    if (this->viewport->get_texture().ptr()->get_image().ptr()->get_format() != Image::Format::FORMAT_RGB8)
    {
        Ref<Image> img = this->viewport->get_texture().ptr()->get_image();
        img->convert(Image::FORMAT_RGB8);
        pixel_data = img->get_data();
    }
    else
        pixel_data = this->viewport->get_texture().ptr()->get_image().ptr()->get_data();
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
