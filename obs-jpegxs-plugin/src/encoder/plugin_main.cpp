#include <obs-module.h>
#include <obs-output.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/threading.h>
#include "obs_jpegxs_output.h"
#include "../ui/jpegxs-dock.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-jpegxs-output", "en-US")

bool obs_module_load(void)
{
    blog(LOG_INFO, "[JPEG XS Output] Plugin loading...");
    
    // Register the JPEG XS output (MUST be static so it persists after function returns!)
    static struct obs_output_info jpegxs_output_info = {};
    register_jpegxs_output(&jpegxs_output_info);
    obs_register_output(&jpegxs_output_info);
    
    // Register UI Dock
    // Note: We leak the dock here, but OBS cleans up docks on exit
    JpegXSDock *dock = new JpegXSDock();
    bool dock_success = obs_frontend_add_custom_qdock("jpegxs_dock", dock);
    
    if (dock_success) {
        blog(LOG_INFO, "[JPEG XS Output] Dock 'jpegxs_dock' added successfully");
    } else {
        blog(LOG_ERROR, "[JPEG XS Output] Failed to add dock 'jpegxs_dock'");
    }

    // Add Tools menu item as backup
    obs_frontend_add_tools_menu_item("JPEG XS Control", [](void *data) {
        blog(LOG_INFO, "[JPEG XS Output] Tools menu item clicked");
        // TODO: Bring dock to front if possible, or show a dialog
    }, nullptr);

    blog(LOG_INFO, "[JPEG XS Output] Plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[JPEG XS Output] Plugin unloading...");
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "JPEG XS low-latency video encoder with RTP over SRT transport";
}

MODULE_EXPORT const char *obs_module_name(void)
{
    return "OBS JPEG XS Output";
}
