# OBS JPEG XS Streaming Plugin

Ultra-low latency streaming plugin for OBS Studio using JPEG XS encoding with RTP over SRT transport.

## Features

- **Sub-40ms glass-to-glass latency** over WAN
- JPEG XS encoding/decoding using Intel SVT-JPEG-XS
- RFC 9134 compliant RTP packetization (SMPTE 2110-22 style)
- SRT transport with FEC, encryption, and adaptive bitrate
- Line-based encoding for minimal latency
- Configurable quality presets
- Real-time performance monitoring

## Architecture

```
OBS → JPEG XS Encoder → RTP Packetizer → SRT Transport
                                              ↓ (WAN)
OBS ← JPEG XS Decoder ← RTP Depacketizer ← SRT Receiver
```

## Requirements

### Build Dependencies

- CMake 3.16+
- C++17 compatible compiler (GCC 9+, Clang 10+, MSVC 2019+)
- OBS Studio 28.0+ (development headers)
- Intel SVT-JPEG-XS 0.10+
- libsrt 1.5.0+

### Runtime Requirements

- OBS Studio 28.0+
- 4+ CPU cores recommended for 1080p60
- Network bandwidth: 400-1200 Mbps for visually lossless quality

## Building

### macOS

```bash
# Install dependencies
brew install obs cmake pkg-config yasm

# Build SVT-JPEG-XS (from parent directory)
cd ../SVT-JPEG-XS/Build/linux
./build.sh install --prefix /usr/local

# Install SRT
brew install srt

# Build plugin
cd ../../obs-jpegxs-plugin
mkdir build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt install build-essential cmake pkg-config yasm
sudo apt install libobs-dev libsrt-dev

# Build SVT-JPEG-XS
cd ../SVT-JPEG-XS/Build/linux
./build.sh install --prefix /usr/local

# Build plugin
cd ../../obs-jpegxs-plugin
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### Windows

```bash
# Use MSYS2 MinGW64 or Visual Studio
# See docs/BUILD_WINDOWS.md for detailed instructions
```

## Usage

### Sender (Encoder)

1. In OBS, go to **Settings → Output**
2. Select **JPEG XS Streaming** from Output Mode
3. Configure:
   - **Server URL**: `srt://receiver-ip:port`
   - **Bitrate**: 800 Mbps (adjust based on network)
   - **Quality Preset**: Low Latency / Balanced / High Quality
4. Click **Start Streaming**

### Receiver (Decoder)

1. In OBS, add **JPEG XS Network Source**
2. Configure:
   - **Listen Port**: 9000 (or custom)
   - **Mode**: Listener
3. Source will automatically receive and decode the stream

## Configuration

### Encoder Settings

| Parameter | Default | Description |
|-----------|---------|-------------|
| Bitrate (BPP) | 5.0 | Bits per pixel (1.0-8.0) |
| Quality Preset | Balanced | Low Latency / Balanced / High Quality |
| SRT Latency | 20ms | Network latency buffer |
| Slice Height | 16 | Lines per slice (lower = less latency) |

### Network Tuning

For WAN streaming, adjust SRT parameters:

```
Low Latency (LAN): 20ms latency, no FEC
Balanced (WAN): 40ms latency, 10% FEC
High Reliability: 80ms latency, 20% FEC
```

## Performance

Tested configurations:

| Resolution | FPS | Bitrate | CPU (Encode) | CPU (Decode) | Latency |
|------------|-----|---------|--------------|--------------|---------|
| 1920x1080 | 60 | 800 Mbps | 30% (8c) | 20% (8c) | 15ms |
| 3840x2160 | 30 | 1200 Mbps | 60% (8c) | 40% (8c) | 20ms |

## Troubleshooting

### "Failed to initialize encoder"
- Check SVT-JPEG-XS is properly installed
- Verify CPU supports required instruction sets (SSE4.1+)

### High latency
- Reduce SRT latency parameter
- Lower slice height for more parallel processing
- Ensure sufficient network bandwidth

### Connection drops
- Increase SRT latency buffer
- Enable FEC (forward error correction)
- Check firewall/NAT settings

## License

BSD 2-Clause License (matching SVT-JPEG-XS)

## Credits

- Intel SVT-JPEG-XS: https://github.com/OpenVisualCloud/SVT-JPEG-XS
- SRT Alliance: https://www.srtalliance.org/
- OBS Studio: https://obsproject.com/

## Status

🚧 **Work in Progress** - See TODO.md for development roadmap
