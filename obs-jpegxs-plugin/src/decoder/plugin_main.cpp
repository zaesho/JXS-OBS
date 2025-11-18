/*
 * OBS JPEG XS Decoder Plugin
 * Main plugin entry point and registration
 */

#include <obs-module.h>
#include <obs-source.h>
#include <util/platform.h>
#include "obs_jpegxs_source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-jpegxs-input", "en-US")

bool obs_module_load(void)
{
    blog(LOG_INFO, "[JPEG XS] Decoder plugin version %s loading...", OBS_PLUGIN_VERSION);
    
    // Register the JPEG XS source
    struct obs_source_info jpegxs_source_info = {};
    register_jpegxs_source(&jpegxs_source_info);
    obs_register_source(&jpegxs_source_info);
    
    blog(LOG_INFO, "[JPEG XS] JPEG XS decoder source registered successfully");
    
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[JPEG XS] Decoder plugin unloading...");
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "JPEG XS low-latency video decoder with RTP over SRT transport";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "OBS JPEG XS Input";
}
