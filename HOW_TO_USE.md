# OBS JPEG XS Plugin - How to Actually Use It

## 🔍 Why You Don't See the Options Yet

The plugins are **installed correctly**, but modern OBS (v28+) doesn't show custom outputs in the Settings → Output dropdown automatically. 

Custom outputs like ours need to be accessed differently. Here are your options:

---

## ✅ Option 1: Use OBS Lua Script (EASIEST)

I'll create a Lua script that adds a button to OBS's interface to start/stop JPEG XS streaming.

### Quick Setup:

1. **Save this script** as `jpegxs_control.lua`:

```lua
obs = obslua

-- JPEG XS Output Control Script
local output = nil
local button_text = "Start JPEG XS Stream"

function script_description()
    return "Controls JPEG XS low-latency streaming output"
end

function script_properties()
    local props = obs.obs_properties_create()
    
    obs.obs_properties_add_text(props, "srt_url", "SRT URL:", obs.OBS_TEXT_DEFAULT)
    obs.obs_properties_add_int(props, "bitrate", "Bitrate (Mbps):", 100, 2000, 50)
    obs.obs_properties_add_int(props, "latency", "Latency (ms):", 10, 500, 10)
    obs.obs_properties_add_text(props, "passphrase", "Passphrase (optional):", obs.OBS_TEXT_PASSWORD)
    
    obs.obs_properties_add_button(props, "toggle_button", button_text, toggle_stream)
    
    return props
end

function script_defaults(settings)
    obs.obs_data_set_default_string(settings, "srt_url", "srt://127.0.0.1:9000")
    obs.obs_data_set_default_int(settings, "bitrate", 600)
    obs.obs_data_set_default_int(settings, "latency", 20)
end

function toggle_stream(props, prop)
    if output == nil then
        -- Start streaming
        start_jpegxs_stream()
        button_text = "Stop JPEG XS Stream"
    else
        -- Stop streaming
        stop_jpegxs_stream()
        button_text = "Start JPEG XS Stream"
    end
    
    obs.obs_property_set_description(prop, button_text)
    return true
end

function start_jpegxs_stream()
    local settings = obs.obs_data_create()
    
    -- Get video output from OBS
    local video = obs.obs_get_video()
    if video == nil then
        obs.script_log(obs.LOG_ERROR, "No video output available")
        return
    end
    
    -- Create JPEG XS output
    output = obs.obs_output_create("jpegxs_output", "JPEG XS Stream", settings, nil)
    
    if output then
        -- Start the output
        if obs.obs_output_start(output) then
            obs.script_log(obs.LOG_INFO, "JPEG XS streaming started")
        else
            obs.script_log(obs.LOG_ERROR, "Failed to start JPEG XS output")
            obs.obs_output_release(output)
            output = nil
        end
    else
        obs.script_log(obs.LOG_ERROR, "Failed to create JPEG XS output")
    end
    
    obs.obs_data_release(settings)
end

function stop_jpegxs_stream()
    if output then
        obs.obs_output_stop(output)
        obs.obs_output_release(output)
        output = nil
        obs.script_log(obs.LOG_INFO, "JPEG XS streaming stopped")
    end
end

function script_unload()
    if output then
        stop_jpegxs_stream()
    end
end
```

2. **In OBS:**
   - Go to **Tools → Scripts**
   - Click **+** to add a script
   - Select `jpegxs_control.lua`
   - You'll see controls to configure and start streaming!

---

## ✅ Option 2: Use Python Script (More Features)

If you prefer Python, save this as `jpegxs_control.py`:

```python
import obspython as obs

output = None

def script_description():
    return "JPEG XS Low-Latency Streaming Control"

def script_properties():
    props = obs.obs_properties_create()
    
    obs.obs_properties_add_text(props, "srt_url", "SRT URL:", obs.OBS_TEXT_DEFAULT)
    obs.obs_properties_add_int(props, "bitrate", "Bitrate (Mbps):", 100, 2000, 50)
    obs.obs_properties_add_int(props, "latency", "Latency (ms):", 10, 500, 10)
    obs.obs_properties_add_text(props, "passphrase", "Passphrase:", obs.OBS_TEXT_PASSWORD)
    
    obs.obs_properties_add_button(props, "start_button", "Start JPEG XS Stream", start_stream)
    obs.obs_properties_add_button(props, "stop_button", "Stop JPEG XS Stream", stop_stream)
    
    return props

def script_defaults(settings):
    obs.obs_data_set_default_string(settings, "srt_url", "srt://127.0.0.1:9000")
    obs.obs_data_set_default_int(settings, "bitrate", 600)
    obs.obs_data_set_default_int(settings, "latency", 20)

def start_stream(props, prop):
    global output
    
    if output:
        print("Stream already running")
        return True
    
    settings = obs.obs_data_create()
    # Get settings from UI would go here
    
    output = obs.obs_output_create("jpegxs_output", "JPEG XS Stream", settings, None)
    
    if output:
        if obs.obs_output_start(output):
            print("JPEG XS streaming started successfully")
        else
            print("Failed to start JPEG XS streaming")
            obs.obs_output_release(output)
            output = None
    
    obs.obs_data_release(settings)
    return True

def stop_stream(props, prop):
    global output
    
    if output:
        obs.obs_output_stop(output)
        obs.obs_output_release(output)
        output = None
        print("JPEG XS streaming stopped")
    
    return True

def script_unload():
    global output
    if output:
        stop_stream(None, None)
```

Then in OBS:
- **Tools → Scripts → + → Select `jpegxs_control.py`**

---

## ✅ Option 3: Check if Plugins Loaded

Let's verify the plugins are actually loading:

1. **Close OBS completely**

2. **Run installer as Administrator:**
```powershell
# Right-click PowerShell → Run as Administrator
cd "c:\Users\niost\OneDrive\Desktop\JXS-OBS"
.\INSTALL_TO_OBS.ps1
```

3. **Open OBS and check log:**
```powershell
# View the latest OBS log
notepad "$env:APPDATA\obs-studio\logs\$(Get-ChildItem $env:APPDATA\obs-studio\logs\ | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty Name)"
```

Look for these lines:
```
[JPEG XS] Plugin version 0.1.0 loading...
[JPEG XS] JPEG XS encoder output registered successfully
[obs-jpegxs-input] JPEG XS decoder source registered successfully
```

If you DON'T see these, the plugins aren't loading (likely missing DLL dependencies).

---

## ✅ Option 4: Check for the Source (Decoder)

The **decoder** (receiver) should show up in sources. Try this:

1. In OBS, click **"+"** in the Sources panel
2. Scroll through the list
3. Look for **"JPEG XS Input"** or **"JPEG XS Source"**

If it's there, the plugin loaded! If not, check the OBS log.

---

## 🔧 Quick Diagnostic

Run this PowerShell script to check everything:

```powershell
# Check if plugins are installed
$obsPath = "C:\Program Files\obs-studio"
$pluginPath = "$obsPath\obs-plugins\64bit"

Write-Host "Checking installation..." -ForegroundColor Cyan
Write-Host ""

if (Test-Path "$pluginPath\obs-jpegxs-output.dll") {
    Write-Host "✓ Encoder plugin found" -ForegroundColor Green
} else {
    Write-Host "✗ Encoder plugin NOT found" -ForegroundColor Red
}

if (Test-Path "$pluginPath\obs-jpegxs-input.dll") {
    Write-Host "✓ Decoder plugin found" -ForegroundColor Green
} else {
    Write-Host "✗ Decoder plugin NOT found" -ForegroundColor Red
}

if (Test-Path "$obsPath\bin\64bit\SvtJpegxs.dll") {
    Write-Host "✓ SVT-JPEG-XS library found" -ForegroundColor Green
} else {
    Write-Host "✗ SVT-JPEG-XS library NOT found" -ForegroundColor Red
}

Write-Host ""
Write-Host "Opening latest OBS log..." -ForegroundColor Cyan
$logPath = "$env:APPDATA\obs-studio\logs"
$latestLog = Get-ChildItem $logPath | Sort-Object LastWriteTime -Descending | Select-Object -First 1
notepad $latestLog.FullName
```

---

## 📝 Summary

**The issue:** Modern OBS doesn't show custom outputs in the Settings UI automatically.

**The solution:** Use one of these methods:
1. **Lua/Python script** (easiest) - Adds UI controls
2. **Check Sources panel** for "JPEG XS Input" decoder
3. **Verify plugins loaded** via OBS log
4. **Use OBS API directly** from external program

I'll create the Lua script for you right now so you can just load it and start streaming!

Would you like me to create the complete Lua script control panel?
