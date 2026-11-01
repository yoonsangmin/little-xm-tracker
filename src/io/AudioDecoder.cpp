// Upstream decoder implementations are compiled unchanged in this one TU.
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include "io/AudioDecoder.hpp"
#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
