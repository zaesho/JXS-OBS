/*
 * OBS JPEG XS Encoder Plugin
 * Main plugin entry point and registration
 */

#include <obs-module.h>
#include <obs-output.h>
#include <util/platform.h>
#include <util/threading.h>
#include "obs_jpegxs_output.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-jpegxs-output", "en-US")

bool obs_module_load(void)
{
    blog(LOG_INFO, "[JPEG XS] Plugin version %s loading...", OBS_PLUGIN_VERSION);
    
    // Register the JPEG XS output
    struct obs_output_info jpegxs_output_info = {};
    register_jpegxs_output(&jpegxs_output_info);
    obs_register_output(&jpegxs_output_info);
    
    blog(LOG_INFO, "[JPEG XS] JPEG XS encoder output registered successfully");
    
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[JPEG XS] Plugin unloading...");
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "JPEG XS low-latency video encoder with RTP over SRT transport";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "OBS JPEG XS Output";
}
