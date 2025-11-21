-- JPEG XS Low-Latency Streaming Control Script for OBS Studio
-- This script provides a UI to control the JPEG XS output plugin

obs = obslua

local output = nil
local is_streaming = false

-- Script information
function script_description()
    return [[<h2>JPEG XS Low-Latency Streaming Control</h2>
<p>Controls the JPEG XS encoder output with SRT transport.</p>
<p><b>Usage:</b></p>
<ol>
<li>Configure your SRT URL and settings below</li>
<li>Click "Start JPEG XS Stream" to begin streaming</li>
<li>Click "Stop JPEG XS Stream" to stop</li>
</ol>
<p><b>Note:</b> This streams your OBS canvas output using JPEG XS codec over SRT.</p>]]
end

-- Define script properties (UI elements)
function script_properties()
    local props = obs.obs_properties_create()
    
    -- SRT connection settings
    obs.obs_properties_add_text(props, "srt_url", "SRT URL:", obs.OBS_TEXT_DEFAULT)
    
    -- Encoder settings
    obs.obs_properties_add_float(props, "bitrate_mbps", "Bitrate (Mbps):", 100.0, 2000.0, 50.0)
    obs.obs_properties_add_int(props, "srt_latency", "Latency (ms):", 10, 500, 5)
    
    -- Security
    obs.obs_properties_add_text(props, "srt_passphrase", "Passphrase (optional):", obs.OBS_TEXT_PASSWORD)

    -- Profile
    obs.obs_properties_add_text(props, "profile", "Profile:", obs.OBS_TEXT_DEFAULT)
    
    -- Control buttons
    obs.obs_properties_add_button(props, "start_button", "Start JPEG XS Stream", start_stream_callback)
    obs.obs_properties_add_button(props, "stop_button", "Stop JPEG XS Stream", stop_stream_callback)
    
    -- Status text
    obs.obs_properties_add_text(props, "status", "Status:", obs.OBS_TEXT_INFO)
    
    return props
end

-- Default values for settings
function script_defaults(settings)
    obs.obs_data_set_default_string(settings, "srt_url", "srt://127.0.0.1:9000?mode=caller")
    obs.obs_data_set_default_double(settings, "bitrate_mbps", 600.0)
    obs.obs_data_set_default_int(settings, "srt_latency", 20)
    obs.obs_data_set_default_string(settings, "srt_passphrase", "")
    obs.obs_data_set_default_string(settings, "profile", "Main420.10")
    obs.obs_data_set_default_string(settings, "status", "Ready")
end

-- Save settings globally
local current_settings = nil

function script_update(settings)
    current_settings = settings
end

-- Start streaming callback
function start_stream_callback(props, prop)
    if is_streaming then
        obs.script_log(obs.LOG_WARNING, "[JPEG XS Control] Already streaming")
        return false
    end
    
    if not current_settings then
        obs.script_log(obs.LOG_ERROR, "[JPEG XS Control] No settings available")
        return false
    end
    
    -- Get settings from UI
    local srt_url = obs.obs_data_get_string(current_settings, "srt_url")
    local bitrate_mbps = obs.obs_data_get_double(current_settings, "bitrate_mbps")
    local srt_latency = obs.obs_data_get_int(current_settings, "srt_latency")
    local srt_passphrase = obs.obs_data_get_string(current_settings, "srt_passphrase")
    local profile = obs.obs_data_get_string(current_settings, "profile")
    
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control] Starting stream...")
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control]   URL: " .. srt_url)
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control]   Bitrate: " .. tostring(bitrate_mbps) .. " Mbps")
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control]   Latency: " .. tostring(srt_latency) .. " ms")
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control]   Profile: " .. profile)
    
    -- Create output settings
    local output_settings = obs.obs_data_create()
    obs.obs_data_set_string(output_settings, "srt_url", srt_url)
    obs.obs_data_set_double(output_settings, "bitrate_mbps", bitrate_mbps)
    obs.obs_data_set_int(output_settings, "srt_latency", srt_latency)
    if srt_passphrase ~= "" then
        obs.obs_data_set_string(output_settings, "srt_passphrase", srt_passphrase)
    end
    obs.obs_data_set_string(output_settings, "profile", profile)
    
    -- Create the JPEG XS output
    output = obs.obs_output_create("jpegxs_output", "JPEG XS Stream", output_settings, nil)
    
    if not output then
        obs.script_log(obs.LOG_ERROR, "[JPEG XS Control] Failed to create output - is the plugin loaded?")
        obs.obs_data_release(output_settings)
        return false
    end
    
    -- Get OBS video output
    local video = obs.obs_get_video()
    local audio = obs.obs_get_audio()
    
    -- Set video/audio for the output (optional, depends on plugin implementation)
    -- obs.obs_output_set_video_encoder(output, encoder)
    
    -- Start the output
    local success = obs.obs_output_start(output)
    
    if success then
        is_streaming = true
        obs.script_log(obs.LOG_INFO, "[JPEG XS Control] Streaming started successfully!")
        obs.obs_data_set_string(current_settings, "status", "Streaming...")
    else
        obs.script_log(obs.LOG_ERROR, "[JPEG XS Control] Failed to start output")
        obs.obs_output_release(output)
        output = nil
        obs.obs_data_set_string(current_settings, "status", "Error starting stream")
    end
    
    obs.obs_data_release(output_settings)
    return true
end

-- Stop streaming callback
function stop_stream_callback(props, prop)
    if not is_streaming or not output then
        obs.script_log(obs.LOG_WARNING, "[JPEG XS Control] Not currently streaming")
        return false
    end
    
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control] Stopping stream...")
    
    obs.obs_output_stop(output)
    obs.obs_output_release(output)
    output = nil
    is_streaming = false
    
    if current_settings then
        obs.obs_data_set_string(current_settings, "status", "Stopped")
    end
    
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control] Stream stopped")
    return true
end

-- Cleanup when script is unloaded
function script_unload()
    if is_streaming and output then
        obs.script_log(obs.LOG_INFO, "[JPEG XS Control] Cleaning up...")
        stop_stream_callback(nil, nil)
    end
end

-- Log plugin info on load
function script_load(settings)
    obs.script_log(obs.LOG_INFO, "[JPEG XS Control] Script loaded - version 1.0")
    
    -- Add menu item if available (this is a bit experimental in Lua)
    -- Note: obs_frontend_add_tools_menu_item might not be exposed in all Lua versions
    if obs.obs_frontend_add_tools_menu_item then
        obs.obs_frontend_add_tools_menu_item("JPEG XS Control", function()
            obs.obs_frontend_open_scripts_dialog()
        end)
    end
end
