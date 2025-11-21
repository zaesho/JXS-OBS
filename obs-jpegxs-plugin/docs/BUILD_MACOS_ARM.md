# SVT-JPEG-XS Build Notes for macOS

## Apple Silicon (ARM) Status: ✅ Supported

SVT-JPEG-XS has been patched to support native compilation on Apple Silicon (M1/M2/M3) Macs.

- **Architecture:** ARM64
- **Mode:** C-Only Fallback (No Assembly)
- **Performance:** ~3-5ms encode/decode latency (slower than x86 ASM, but viable for testing and development)

## Build Instructions (Native)

You do **not** need Rosetta 2. You can build natively.

### 1. Prerequisites

Install dependencies via Homebrew:

```bash
brew install cmake srt qt@6
brew install svt-av1  # Optional, for reference
```

### 2. Build SVT-JPEG-XS (Patched)

The included submodule has been patched to detect ARM64 and automatically disable x86 assembly optimizations.

```bash
cd obs-jpegxs-plugin/SVT-JPEG-XS
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=/usr/local
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### 3. Build OBS Plugin

```bash
cd ../../  # Back to obs-jpegxs-plugin root
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCPACK_GENERATOR="Bundle"
make -j$(sysctl -n hw.ncpu)
make install
```

## Troubleshooting

### "Target architecture not supported"
If you see errors related to `AVX` or `SSE` flags, ensure you are using the patched `CMakeLists.txt` in the `SVT-JPEG-XS` directory. The patch conditionally disables these flags when `CMAKE_SYSTEM_PROCESSOR` matches `arm64`.

### Performance Warning
You will see a warning during configuration:
`Compiling for non-x86 architecture: arm64`

This is expected. The build will proceed using portable C code.
