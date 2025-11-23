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
    // We must create the dock only when the frontend is fully loaded, 
    // otherwise Qt might not be ready or the dock might not attach correctly.
    obs_frontend_add_event_callback([](enum obs_frontend_event event, void *private_data) {
        if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
            blog(LOG_INFO, "[JPEG XS Output] Frontend loaded, creating dock...");
            static JpegXSDock *dock = new JpegXSDock();
            obs_frontend_add_dock_by_id("jpegxs_manager", "JPEG XS Manager", dock);
            
            // Also register tools menu item that references this dock
             obs_frontend_add_tools_menu_item("JPEG XS Manager", [](void *data) {
                // This is handled by the dock system usually, but we can force show
                // Actually, if we used add_dock_by_id, it should be in the Docks menu automatically.
                // But let's keep a tools shortcut that ensures it pops up.
                if (dock) {
                    dock->setVisible(true);
                    dock->raise();
                }
            }, nullptr);
        }
    }, nullptr);

    /*
    // Old synchronous registration (removed)
    static JpegXSDock *dock = nullptr;
    dock = new JpegXSDock();
    bool dock_success = obs_frontend_add_custom_qdock("jpegxs_dock", dock);
    */
    
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
