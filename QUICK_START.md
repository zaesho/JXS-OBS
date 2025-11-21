# OBS JPEG XS Plugin - Quick Start Guide

## 🎯 What We Built

A complete low-latency video streaming solution for OBS Studio using JPEG XS compression and SRT transport.

**Key Features:**
- Ultra-low latency (<30ms glass-to-glass)
- Professional JPEG XS codec (SVT-JPEG-XS)
- Secure, reliable streaming (SRT protocol)
- RFC 9134 compliant RTP packetization
- Windows x86_64 native

## 📦 Installation

### Prerequisites
- OBS Studio 30.x or later installed
- Windows 10/11 (64-bit)

### Install Plugin DLLs

1. **Copy the plugins:**
```powershell
# Replace with your OBS installation path
$obsPath = "C:\Program Files\obs-studio"

Copy-Item "c:\Users\niost\OneDrive\Desktop\JXS-OBS\obs-jpegxs-plugin\build\Release\obs-jpegxs-output.dll" `
          "$obsPath\obs-plugins\64bit\"

Copy-Item "c:\Users\niost\OneDrive\Desktop\JXS-OBS\obs-jpegxs-plugin\build\Release\obs-jpegxs-input.dll" `
          "$obsPath\obs-plugins\64bit\"
```

2. **Copy required libraries:**
```powershell
# SVT-JPEG-XS codec
Copy-Item "c:\Users\niost\OneDrive\Desktop\JXS-OBS\install\svt-jpegxs\lib\SvtJpegxs.dll" `
          "$obsPath\bin\64bit\"

# SRT streaming library
Copy-Item "C:\Program Files (x86)\libsrt\bin\*.dll" `
          "$obsPath\bin\64bit\"
```

3. **Restart OBS Studio**

## 🚀 Basic Usage

### Scenario 1: Local Testing (Same Computer)

**Setup Encoder (Sender):**
1. Open OBS Studio
2. Add a video source (e.g., Display Capture, Webcam)
3. Go to **Settings → Output**
4. Change Output Mode to **"Advanced"**
5. In Streaming tab, set:
   - Type: **JPEG XS Output**
   - SRT URL: `srt://127.0.0.1:9000?mode=caller`
   - Bitrate: `600` Mbps (for 1080p60)
   - Latency: `20` ms
6. Click **Apply**

**Setup Decoder (Receiver):**
1. Open **another instance** of OBS Studio
2. Add **Source → JPEG XS Input**
3. Configure:
   - SRT URL: `srt://:9000?mode=listener`
   - Latency: `20` ms
4. Click **OK**

**Test:**
1. In first OBS: Click **Start Streaming**
2. In second OBS: You should see the video appear!
3. Check latency using a timer or stopwatch

### Scenario 2: Network Streaming (Two Computers)

**Computer A (Sender):**
1. Note your IP address (run `ipconfig` in PowerShell)
   - Example: `192.168.1.100`
2. Open OBS, configure JPEG XS Output:
   - SRT URL: `srt://RECEIVER_IP:9000?mode=caller`
   - Replace `RECEIVER_IP` with Computer B's IP
3. Start Streaming

**Computer B (Receiver):**
1. Ensure port 9000 is open in firewall:
   ```powershell
   New-NetFirewallRule -DisplayName "SRT Stream" -Direction Inbound -Protocol UDP -LocalPort 9000 -Action Allow
   ```
2. Open OBS, add JPEG XS Input source:
   - SRT URL: `srt://:9000?mode=listener`
3. Video should appear automatically

## ⚙️ Configuration Reference

### Bitrate Guidelines

| Resolution | FPS | Recommended Bitrate | Minimum Network |
|------------|-----|---------------------|-----------------|
| 720p | 30 | 200 Mbps | 250 Mbps |
| 720p | 60 | 300 Mbps | 350 Mbps |
| 1080p | 30 | 400 Mbps | 500 Mbps |
| 1080p | 60 | 600 Mbps | 700 Mbps |
| 4K | 30 | 800 Mbps | 1 Gbps |
| 4K | 60 | 1200 Mbps | 1.5 Gbps |

### SRT Latency Settings

| Use Case | Latency | Notes |
|----------|---------|-------|
| LAN | 20ms | Minimum, local network only |
| Campus WAN | 40ms | Within building/campus |
| City WAN | 60-80ms | Same city, <10ms RTT |
| Internet | 100-200ms | Public internet, variable RTT |

### SRT URL Format

**Sender (Caller Mode):**
```
srt://RECEIVER_IP:PORT?mode=caller&latency=20&passphrase=SECRET
```

**Receiver (Listener Mode):**
```
srt://:PORT?mode=listener&latency=20&passphrase=SECRET
```

**Parameters:**
- `mode`: `caller` (sender) or `listener` (receiver)
- `latency`: Buffer time in milliseconds (default: 20)
- `passphrase`: Optional AES encryption key (16-32 chars)

## 🔧 Troubleshooting

### Plugin Not Showing Up
**Problem:** JPEG XS options missing in OBS

**Solutions:**
1. Check OBS log: `%APPDATA%\obs-studio\logs\`
2. Verify all DLLs copied to correct folders
3. Check DLL dependencies with [Dependencies Tool](https://github.com/lucasg/Dependencies)
4. Ensure OBS is 64-bit version

### Connection Failed
**Problem:** Decoder shows "Connecting..." but never receives

**Solutions:**
1. **Firewall:** Add rule for UDP port 9000
   ```powershell
   New-NetFirewallRule -DisplayName "SRT" -Protocol UDP -LocalPort 9000 -Action Allow
   ```
2. **Network:** Ping receiver from sender
   ```powershell
   ping RECEIVER_IP
   ```
3. **Mode:** Sender must be `caller`, receiver must be `listener`
4. **Port:** Ensure both use same port number

### Poor Quality / Artifacts
**Problem:** Video looks compressed or blocky

**Solutions:**
1. **Increase bitrate:** See guidelines above
2. **Check CPU:** Encoding shouldn't exceed 50% CPU
3. **Network:** Ensure bandwidth is 20-30% above bitrate
4. **Resolution:** Lower resolution if network is limited

### High Latency
**Problem:** Noticeable delay (>100ms)

**Solutions:**
1. **Reduce SRT latency:** Try 10-15ms on LAN
2. **Check network:** Run `iperf3` bandwidth test
3. **CPU load:** High CPU can cause delays
4. **Frame rate:** Lower FPS reduces processing time

## 📊 Monitoring Performance

### Check OBS Log
```powershell
# View latest log
notepad "$env:APPDATA\obs-studio\logs\$(Get-ChildItem $env:APPDATA\obs-studio\logs\ | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty Name)"
```

Look for lines containing:
- `[JPEG XS]` - Plugin messages
- `Encoding failed` - Encoder errors
- `SRT send failed` - Network issues

### Network Bandwidth Test
```powershell
# Install iperf3
winget install iperf3

# On receiver:
iperf3 -s

# On sender:
iperf3 -c RECEIVER_IP -u -b 700M -t 10
```

This tests if your network can handle the bitrate.

### CPU Usage
- Open Task Manager → Performance → CPU
- While streaming, encoding should use 15-30% CPU
- If >50%, reduce resolution or bitrate

## 🎓 Advanced Configuration

### Encrypted Streaming
Add passphrase to SRT URL:
```
Sender: srt://IP:9000?mode=caller&passphrase=MySecretKey123
Receiver: srt://:9000?mode=listener&passphrase=MySecretKey123
```

Passphrase must be:
- 16-32 characters
- Same on both sides
- Uses AES-128/192/256 encryption

### Multiple Streams
Use different ports for each stream:
```
Stream 1: Port 9000
Stream 2: Port 9001
Stream 3: Port 9002
```

Each needs its own firewall rule.

### WAN Streaming
For internet streaming:
1. **Port forwarding:** Forward UDP port 9000 on receiver's router
2. **Higher latency:** Use 100-200ms SRT latency
3. **Lower bitrate:** Internet may be limited (test with iperf3)
4. **Static IP:** Receiver needs static or dynamic DNS

## 🐛 Known Issues

1. **URL Parsing:** Currently requires manual `IP:PORT` format
2. **Statistics:** No built-in UI for bitrate/latency stats yet
3. **Format:** Only YUV 4:2:0 8-bit tested (most common)
4. **Auto-reconnect:** May need manual restart on network interruption

## 📚 Additional Resources

### Files
- `BUILD_STATUS.md` - Complete build documentation
- `DEVELOPMENT.md` - Technical implementation details
- `README.md` - Project overview

### Logs Location
- Windows: `%APPDATA%\obs-studio\logs\`
- Each OBS session creates a new log file

### Network Tools
- **Wireshark:** Capture network packets
- **iperf3:** Bandwidth testing
- **PingPlotter:** Network latency analysis

## ✅ Quick Checklist

Before streaming:
- [ ] Plugins installed to OBS
- [ ] All DLLs copied to `bin\64bit\`
- [ ] Firewall allows UDP port
- [ ] Network bandwidth tested
- [ ] Encoder configured with SRT URL
- [ ] Decoder listening on same port
- [ ] Same passphrase (if using encryption)

## 🎉 Success!

If you can see video on the receiver with <30ms delay, congratulations! You have a working professional-grade low-latency streaming setup.

For questions or issues, check the detailed `BUILD_STATUS.md` file.

---

*Quick Start Guide - November 17, 2025*
