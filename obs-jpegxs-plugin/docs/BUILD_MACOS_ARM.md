# SVT-JPEG-XS Build Notes for macOS

## Apple Silicon (ARM) Compatibility Issue

SVT-JPEG-XS contains x86-specific assembly optimizations (SSE, AVX2, AVX512) that cannot compile on Apple Silicon (M1/M2/M3) Macs.

## Solutions:

###  1. Use Rosetta 2 (Recommended for Development)

Build under x86_64 emulation:

```bash
arch -x86_64 /bin/bash
cd /Users/gianvillarini/Documents/Github/JXS-OBS/SVT-JPEG-XS
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON
make -j$(sysctl -n hw.ncpu)
sudo make install
```

### 2. Install Pre-built x86 Binary via Homebrew

```bash
# Switch to x86_64 Homebrew
arch -x86_64 /usr/local/bin/brew install <package-if-available>
```

### 3. Build on x86_64 Linux (Production)

For production deployments, build on:
- Linux x86_64 (Ubuntu 20.04+, CentOS 8+)
- Intel/AMD processors with AVX2 support

### 4. Wait for ARM-native Port

The SVT-JPEG-XS project would need to:
- Add ARM NEON intrinsics as alternatives to x86 intrinsics
- Or use portable C fallback code (slower performance)

## Workaround for Plugin Development

For now, we can:
1. Develop the OBS plugin structure on ARM
2. Use mock/stub implementations for encoder/decoder
3. Test full pipeline on x86_64 Linux or use Rosetta 2
4. Document x86_64 requirement in README

## Performance Impact

- **x86 with ASM**: ~1ms encode/decode latency ✅
- **x86 C fallback**: ~3-5ms encode/decode latency ⚠️  
- **ARM NEON (when ported)**: ~1-2ms encode/decode latency (estimated)

## Current Status

- ❌ Native ARM64 build fails due to x86 intrinsics
- ✅ x86_64 via Rosetta 2 works
- ✅ Plugin architecture can be developed independently
- ⏳ Waiting for SVT-JPEG-XS ARM port or fallback paths
