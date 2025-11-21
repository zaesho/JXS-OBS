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
    // MINIMAL VERSION - no logging, no complex operations
    // Just register the source
    
    // Register the JPEG XS source (MUST be static so it persists after function returns!)
    static struct obs_source_info jpegxs_source_info = {};
    register_jpegxs_source(&jpegxs_source_info);
    obs_register_source(&jpegxs_source_info);
    
    return true;
}

void obs_module_unload(void)
{
    // MINIMAL VERSION - no logging
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "JPEG XS low-latency video decoder with RTP over SRT transport";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "OBS JPEG XS Input";
}
