# Godot-GStreamer-Camera
Ever wanted to have a virtual camera in Godot which can output through gstreamer? then you can use this!

This project is made as a part of a [autonomous vehicle simulator](https://github.com/MoeenFJ/AVS) intended to be used for training 1/10th scale autonomous vehicles.

Disclaimer : A portion of this addon is made using AI.

## How to use
1. Add the bin folder and the GStreamerCamera.gdextension file to your project. make sure the paths in GStreamerCamera.gdextension match your project directory structure. The default is : "res://addons/Godot-GStreamer-Camera/bin".
2. Restart the editor.
3. Now a new node called "GStreamerCamera" should have been added.
4. After adding one to the scene, you can configure it in the editor.
5. After running the project, a new socket file with the name of the camera is created in the specified directory. This is a "shmsink" file which can be read using a gstreamer pipeline. The default gstreamer pipeline string is also printed and can be edited in the editor.

Python code to open the camera with the default pipeline  in OpenCV:
```Python
import cv2
GST_PIPELINE = (
    "shmsrc socket-path=/tmp/VirtualCamera is-live=true do-timestamp=true ! "
    "video/x-raw,format=I420,width=256,height=256,framerate=30/1 ! "
    "videoconvert ! "
    "video/x-raw,format=BGR ! "
    "appsink drop=true sync=false"
)
cap = cv2.VideoCapture(GST_PIPELINE, cv2.CAP_GSTREAMER)
```