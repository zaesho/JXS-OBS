/* Generated obsconfig.h for OBS plugin development */

#pragma once

#define OBS_VERSION "32.0.2"
#define OBS_DATA_PATH "data"
#define OBS_INSTALL_PREFIX "/usr/local"
#define OBS_PLUGIN_DESTINATION "obs-plugins"
#define OBS_RELATIVE_PREFIX "../../"

/* Platform detection */
#if defined(__APPLE__)
#define __APPLE__ 1
#define ENABLE_AUDIO_ENCODER_SNDIO 0
#elif defined(_WIN32)
#define _WIN32 1
#else
#define __linux__ 1
#endif

/* Build configuration */
#define BUILD_CAPTIONS 0
#define ENABLE_HEVC 1
#define ENABLE_WAYLAND 0
#define ENABLE_BROWSER 0
