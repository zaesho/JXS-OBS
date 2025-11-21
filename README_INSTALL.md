# OBS JPEG XS Plugin - Installation Guide

## Quick Install (On This Computer)

Run as Administrator:
```powershell
.\UNIVERSAL_INSTALL.ps1
```

## Installing on Another Computer

### Option 1: Using Pre-Built Files

1. **Copy these files** to the target computer:
   ```
   obs-jpegxs-plugin/build/Release/obs-jpegxs-input.dll
   obs-jpegxs-plugin/build/Release/obs-jpegxs-output.dll
   SVT-JPEG-XS/Build/windows/Release/SvtJpegxsEnc.dll
   UNIVERSAL_INSTALL.ps1
   ```

2. **Run the installer** as Administrator:
   ```powershell
   .\UNIVERSAL_INSTALL.ps1
   ```

### Option 2: Manual Installation

1. **Locate your OBS installation** (usually one of these):
   - `C:\Program Files\obs-studio`
   - `C:\Program Files (x86)\obs-studio`
   - `%LOCALAPPDATA%\Programs\obs-studio`

2. **Copy plugin DLLs**:
   - Copy `obs-jpegxs-input.dll` and `obs-jpegxs-output.dll`
   - To: `[OBS Path]\obs-plugins\64bit\`

3. **Copy SVT library**:
   - Copy `SvtJpegxsEnc.dll`
   - To: `[OBS Path]\bin\64bit\`

4. **Restart OBS** if it was running

## Verifying Installation

1. Open OBS Studio
2. Check the log (Help → Log Files → View Current Log)
3. Look for these lines:
   ```
   Loaded Modules:
     obs-jpegxs-output.dll
     obs-jpegxs-input.dll
   ```

## Usage

### Decoder (Receiving Video)
1. Add a new Source → "JPEG XS Source (RTP/SRT)"
2. Set SRT URL (e.g., `srt://0.0.0.0:5000` for listener mode)
3. Video will appear when encoder starts

### Encoder (Sending Video)
1. Add a new Output → "JPEG XS Output (RTP/SRT)"
2. Set SRT URL (e.g., `srt://192.168.1.100:5000?mode=caller`)
3. Set compression ratio (default: 10:1)
4. Start streaming

## Troubleshooting

- **No video output**: Make sure BGRA format is being used (this is automatic)
- **Port conflicts**: Change the port in the SRT URL (e.g., `:5001` instead of `:5000`)
- **Connection failed**: Check firewall settings and network connectivity
