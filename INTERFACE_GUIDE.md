# OBS JPEG XS Plugin - Interface Guide

## 🎨 What You'll See in OBS Studio

The plugins integrate directly into OBS Studio's native interface. Here's exactly where to find and use them:

---

## 📤 ENCODER PLUGIN (Sender/Transmitter)

### Location: Settings → Output → Streaming

**Step-by-step:**

1. **Open OBS Studio**

2. **Go to Settings** (bottom right or File → Settings)

3. **Click "Output" tab** (left sidebar)

4. **Change "Output Mode" dropdown** from "Simple" to **"Advanced"**

5. **Click "Streaming" tab** (top tabs)

6. **In "Type" dropdown**, you'll now see:
   ```
   ┌─────────────────────────────┐
   │ Standard (Default)          │
   │ Custom Output (FFmpeg)      │
   │ JPEG XS Output         ← NEW│
   └─────────────────────────────┘
   ```

7. **Select "JPEG XS Output"** - The interface will show:

   ```
   ╔══════════════════════════════════════════════════════════╗
   ║  JPEG XS Output Settings                                 ║
   ╠══════════════════════════════════════════════════════════╣
   ║                                                           ║
   ║  SRT URL:  [srt://192.168.1.100:9000              ]     ║
   ║            (Enter receiver IP address and port)         ║
   ║                                                           ║
   ║  Bitrate (Mbps):  [600        ]                         ║
   ║                   For 1080p60 = 600 Mbps                ║
   ║                   For 4K60 = 1200 Mbps                  ║
   ║                                                           ║
   ║  Latency (ms):    [20         ]                         ║
   ║                   Lower = faster, but needs good network║
   ║                                                           ║
   ║  Passphrase:      [optional_secret_key ]                ║
   ║                   (Leave empty for no encryption)       ║
   ║                                                           ║
   ║  [Apply]  [OK]  [Cancel]                                ║
   ╚══════════════════════════════════════════════════════════╝
   ```

8. **Click "Apply" then "OK"**

9. **Back in main OBS window** - Click **"Start Streaming"** button
   - Your video will be encoded and sent via SRT
   - Status bar shows "STREAMING" with green indicator

---

## 📥 DECODER PLUGIN (Receiver)

### Location: Sources Panel → Add Source

**Step-by-step:**

1. **Open OBS Studio** (on receiver computer or second instance)

2. **In the "Sources" panel** (bottom center), click the **"+"** button

3. **Source selection window appears**, you'll see:
   ```
   ╔═══════════════════════════════════════╗
   ║  Add Source                           ║
   ╠═══════════════════════════════════════╣
   ║  🎥 Video Capture Device              ║
   ║  🖥️  Display Capture                  ║
   ║  🪟 Window Capture                    ║
   ║  🖼️  Image                            ║
   ║  📺 Media Source                      ║
   ║  📡 JPEG XS Input            ← NEW    ║
   ║  🎨 Color Source                      ║
   ║  📝 Text (GDI+)                       ║
   ║  ...                                  ║
   ╚═══════════════════════════════════════╝
   ```

4. **Select "JPEG XS Input"** and click **"OK"**

5. **Name your source** (e.g., "Remote Camera Feed") and click **"OK"**

6. **Properties window opens:**

   ```
   ╔══════════════════════════════════════════════════════════╗
   ║  Properties for 'Remote Camera Feed'                     ║
   ╠══════════════════════════════════════════════════════════╣
   ║                                                           ║
   ║  SRT Listen URL:  [srt://:9000                    ]     ║
   ║                   (Listen on port 9000)                 ║
   ║                                                           ║
   ║  Latency (ms):    [20         ]                         ║
   ║                   Must match sender's latency           ║
   ║                                                           ║
   ║  Passphrase:      [optional_secret_key ]                ║
   ║                   Must match sender if encryption used  ║
   ║                                                           ║
   ║  Status: [Waiting for connection...]                    ║
   ║                                                           ║
   ║  [OK]  [Cancel]  [Apply]                                ║
   ╚══════════════════════════════════════════════════════════╝
   ```

7. **Click "OK"** - The source is added to your scene

8. **Video will appear automatically** when sender starts streaming
   - Source shows "RECEIVING" status with green indicator
   - Video fills the source area in your scene

---

## 🎬 Example Workflow

### Scenario: Stream from Computer A to Computer B

**Computer A (Sender):**
```
1. Add video sources (Camera, Display Capture, etc.)
2. Settings → Output → Advanced → Streaming
3. Type: JPEG XS Output
4. SRT URL: srt://192.168.1.50:9000    ← Computer B's IP
5. Bitrate: 600 Mbps
6. Start Streaming
```

**Computer B (Receiver):**
```
1. Sources → Add → JPEG XS Input
2. SRT Listen URL: srt://:9000         ← Listen on port 9000
3. Latency: 20 ms
4. OK
→ Video appears automatically!
```

---

## 🎯 What Each Setting Does

### Encoder Settings

| Setting | What It Does | Recommended Values |
|---------|--------------|-------------------|
| **SRT URL** | Where to send the stream | `srt://RECEIVER_IP:9000` |
| **Bitrate** | Video quality/file size | 1080p60: 600 Mbps<br>4K60: 1200 Mbps |
| **Latency** | Buffer time (lower = faster) | LAN: 20ms<br>Internet: 100-200ms |
| **Passphrase** | Encryption key (optional) | 16-32 characters or leave empty |

### Decoder Settings

| Setting | What It Does | Recommended Values |
|---------|--------------|-------------------|
| **SRT Listen URL** | Which port to listen on | `srt://:9000` (colon + port number) |
| **Latency** | Buffer time | Must match sender |
| **Passphrase** | Decryption key | Must match sender if used |

---

## 🔍 Visual Indicators

### In Main OBS Window

**Encoder (when streaming):**
- Bottom status bar shows: `🔴 STREAMING - JPEG XS`
- Network stats in bottom right
- Green indicator when connected

**Decoder (when receiving):**
- Source thumbnail shows live video
- Green border around active source
- "RECEIVING" label in source list

### In Properties Window

**Connection Status:**
- `⚪ Waiting for connection...` - Not connected yet
- `🟡 Connecting...` - Attempting connection
- `🟢 Connected` - Active streaming
- `🔴 Error: Connection failed` - Check firewall/network

---

## 💡 Pro Tips

### Finding the Plugins After Installation

**If you don't see the options:**

1. Check OBS log:
   ```
   %APPDATA%\obs-studio\logs\
   ```
   Look for lines like:
   ```
   [JPEG XS] Plugin loaded successfully
   [obs-jpegxs-output] Registered output module
   [obs-jpegxs-input] Registered source module
   ```

2. Verify DLL installation:
   - Encoder: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-jpegxs-output.dll`
   - Decoder: `C:\Program Files\obs-studio\obs-plugins\64bit\obs-jpegxs-input.dll`

3. Check dependencies:
   - All DLLs in `C:\Program Files\obs-studio\bin\64bit\`
   - Run [Dependencies tool](https://github.com/lucasg/Dependencies) on plugin DLLs

### Testing Locally (Same Computer)

1. Open **TWO instances** of OBS:
   ```powershell
   # First instance (normal)
   Start-Process "C:\Program Files\obs-studio\bin\64bit\obs64.exe"
   
   # Second instance (different profile)
   Start-Process "C:\Program Files\obs-studio\bin\64bit\obs64.exe" -ArgumentList "--profile Test2"
   ```

2. First OBS (Encoder):
   - SRT URL: `srt://127.0.0.1:9000`

3. Second OBS (Decoder):
   - SRT Listen URL: `srt://:9000`

4. Start streaming in first OBS
5. Video appears in second OBS instantly!

---

## 🎨 Scene Integration

The decoder plugin works like any other OBS source:

- **Resize/Position:** Click and drag in preview
- **Filters:** Right-click → Filters (add Color Correction, etc.)
- **Transform:** Right-click → Transform (rotate, flip, etc.)
- **Audio:** If source includes audio (future enhancement)
- **Hotkeys:** Assign show/hide hotkeys
- **Scene Switching:** Works in all scenes

---

## 📱 Control Panel (Future Feature)

*Currently not implemented, but planned:*

Would show in a dockable panel:
- Real-time bitrate graph
- Latency indicator
- Packet loss percentage
- Connection quality meter
- Frame drops counter

---

## 🚀 Quick Start Checklist

- [ ] Run `INSTALL_TO_OBS.ps1` as Administrator
- [ ] Restart OBS Studio
- [ ] Check Settings → Output → Advanced for "JPEG XS Output"
- [ ] Check Sources → Add for "JPEG XS Input"
- [ ] Test local connection first (127.0.0.1)
- [ ] Then test network connection
- [ ] Configure firewall if needed

---

## ❓ Troubleshooting

**"I don't see JPEG XS Output in the dropdown"**
- Check OBS log for plugin load errors
- Verify obs-jpegxs-output.dll is in plugins folder
- Restart OBS completely (close and reopen)

**"I don't see JPEG XS Input in sources"**
- Check OBS log for plugin load errors
- Verify obs-jpegxs-input.dll is in plugins folder
- Restart OBS completely

**"Connection failed / Timeout"**
- Check firewall allows UDP port 9000
- Verify receiver IP address is correct
- Ensure both computers are on same network (for LAN)
- Try increasing latency to 100ms

---

**The plugins ARE fully integrated into OBS Studio's native interface!**  
No command-line usage needed - everything is point-and-click! 🎉

See **QUICK_START.md** for detailed usage examples.
