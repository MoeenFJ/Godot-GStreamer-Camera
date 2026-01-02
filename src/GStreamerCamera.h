#ifndef GSTREAMER_CAMERA_H
#define GSTREAMER_CAMERA_H

// --- Godot GDExtension Includes ---
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

// --- GStreamer Library Includes ---
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

using namespace godot;

class GStreamerCamera : public Sprite2D {
    GDCLASS(GStreamerCamera, Sprite2D)

private:
    // --- Configurable Variables (Set by initialize()) ---
    int frame_width = 0;
    int frame_height = 0;
    String device_name; 
    String device_path;
    String pipeline_string;

    // --- GStreamer Elements ---
    GstElement *pipeline = nullptr; // The main container
    GstAppSrc *appsrc = nullptr;    // The element we push raw data into

    // --- State and Timing ---
    guint64 pts = 0; // Presentation Timestamp counter
    bool is_streaming = false;

protected:
    static void _bind_methods();

public:
    GStreamerCamera();
    ~GStreamerCamera();

    void _process(double delta);

    void initialize(int width, int height, const String& device_name, const String& pipeline_string);

    void send_frame(const PackedByteArray& pixel_data);
    
    void stop_stream();
    
    bool is_streaming_active() const { return is_streaming; }
};

#endif 
