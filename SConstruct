#!/usr/bin/env python
import os
import platform
import sys

env = SConscript("godot-cpp/SConstruct")

# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")
env.ParseConfig('pkg-config gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 glib-2.0 --cflags --libs')

opts = Variables([], ARGUMENTS)
opts.Add(EnumVariable("arch", "CPU Architecture", platform.machine().lower(), ["x86_64", "x86_32", "arm64","aarch64", "rv64"]))
opts.Update(env)

# 2. Fix common naming inconsistencies (e.g., 'amd64' vs 'x86_64')
if env["arch"] == "amd64":
    env["arch"] = "x86_64"
elif env["arch"] == "armv8" or env["arch"] == "aarch64":
    env["arch"] = "arm64"

library = env.SharedLibrary(
    f"bin/libGStreamerCamera.{env["arch"]}{env["SHLIBSUFFIX"]}",
    source=sources,
)

Default(library)
