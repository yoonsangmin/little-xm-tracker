#pragma once

// Decoding only: SDL and MilkyPlay retain all device/playback ownership.
#define MA_NO_DEVICE_IO
#define MA_NO_THREADING
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#define MA_NO_RUNTIME_LINKING
#define MA_NO_STDIO
// PCM unsigned8 is centered on128 and scaled by128, matching XM's signed8.
#define MA_DR_WAV_LIBSNDFILE_COMPAT
#include <miniaudio.h>
