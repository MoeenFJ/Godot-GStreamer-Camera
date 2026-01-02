#ifndef GSTREAMER_CAMERA_H
#define GSTREAMER_CAMERA_H

// --- Godot GDExtension Includes ---
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/camera3d.hpp>
// --- GStreamer Library Includes ---
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

using namespace godot;

class GStreamerCamera : public Node3D {
    GDCLASS(GStreamerCamera, Node3D)

private:
    
    Vector2i frameSize = Vector2i(256,256);
    String deviceName = "VirtualCamera"; 
    String devicePath = "./VirtualCamera";

    String pipelineString = "";

    // --- GStreamer Elements ---
    GstElement *pipeline = nullptr; // The main container
    GstAppSrc *appsrc = nullptr;    // The element we push raw data into

    // --- State and Timing ---
    guint64 pts = 0; // Presentation Timestamp counter
    bool is_streaming = false;
    Image::Format imageFormat = Image::Format::FORMAT_RGB8;

    SubViewport* viewport;
    Camera3D* camera;


    void initializeGStreamer();

    void send_frame();
    
    void stop_stream();

protected:
    static void _bind_methods();

    

public:
    GStreamerCamera();
    ~GStreamerCamera();

    void set_device_name(const String name);
    String get_device_name() const;

    void set_frame_size(const Vector2i size);
    Vector2i get_frame_size() const;

    void set_pipeline_string(const String pipelineString);
    String get_pipeline_string() const;
    
    void _process(double delta) override;
    void _ready() override;

    bool is_streaming_active() const { return is_streaming; }
};

#endif 
