#!/bin/bash
set -e

# Ensure we are in the script's directory
cd "$(dirname "$0")"

# Colors for output
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting OBS JPEG-XS Plugin Install for macOS ARM...${NC}"

# 1. Install Dependencies
echo -e "${GREEN}Installing dependencies via Homebrew...${NC}"
brew install cmake srt qt@6 svt-av1

# 2. Patch SVT-JPEG-XS for ARM64
echo -e "${GREEN}Patching SVT-JPEG-XS for ARM64...${NC}"

# Apply NEON patch if available
if [ -f "patches/svt-jpegxs-neon.patch" ]; then
    echo "Applying NEON patch..."
    cd SVT-JPEG-XS
    # check if already applied to avoid errors? git apply is usually safe if check fails
    git apply --check ../patches/svt-jpegxs-neon.patch 2>/dev/null && git apply ../patches/svt-jpegxs-neon.patch || echo "Patch likely already applied."
    cd ..
fi

# Patch SVT-JPEG-XS/CMakeLists.txt to disable x86 ASM on ARM64
if ! grep -q "Compiling for ARM64" SVT-JPEG-XS/CMakeLists.txt; then
    perl -i -0777 -pe 's/enable_language\(ASM_NASM\)\nadd_definitions\(-DARCH_X86_64=1\)/if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")\n    message(STATUS "Compiling for ARM64 - Disabling x86 Assembly")\nelse()\n    enable_language(ASM_NASM)\n    add_definitions(-DARCH_X86_64=1)\nendif()/g' SVT-JPEG-XS/CMakeLists.txt
fi

# Patch SVT-JPEG-XS/Source/Lib/CMakeLists.txt to exclude ASM dirs
if ! grep -q "CMAKE_SYSTEM_PROCESSOR MATCHES \"arm64\"" SVT-JPEG-XS/Source/Lib/CMakeLists.txt; then
    perl -i -0777 -pe 's/(add_subdirectory\(Common\/ASM_SSE2\)\n.*add_subdirectory\(Decoder\/ASM_AVX512\))/if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"))\n$1\nendif()/gs' SVT-JPEG-XS/Source/Lib/CMakeLists.txt

    perl -i -0777 -pe 's/(\$<TARGET_OBJECTS:COMMON_CODEC>\n.*\$<TARGET_OBJECTS:DECODER_ASM_SSE4_1>\n    )/\$<TARGET_OBJECTS:COMMON_CODEC>\n    \$<TARGET_OBJECTS:ENCODER_CODEC>\n    \$<TARGET_OBJECTS:DECODER_CODEC>\n    )\n\nif(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64"))\n    target_sources(SvtJpegxsLib PRIVATE\n        \$<TARGET_OBJECTS:COMMON_ASM_AVX2>\n        \$<TARGET_OBJECTS:COMMON_ASM_SSE2>\n        \$<TARGET_OBJECTS:ENCODER_ASM_SSE2>\n        \$<TARGET_OBJECTS:ENCODER_ASM_SSE4_1>\n        \$<TARGET_OBJECTS:ENCODER_ASM_AVX2>\n        \$<TARGET_OBJECTS:ENCODER_ASM_AVX512>\n        \$<TARGET_OBJECTS:DECODER_ASM_AVX512>\n        \$<TARGET_OBJECTS:DECODER_ASM_AVX2>\n        \$<TARGET_OBJECTS:DECODER_ASM_SSE4_1>\n    )\nendif()\n# Original block removed/gs' SVT-JPEG-XS/Source/Lib/CMakeLists.txt
fi

# Patch common_dsp_rtcd.h to expose get_cpu_flags
if grep -q "#ifdef ARCH_X86_64" SVT-JPEG-XS/Source/Lib/Common/Codec/common_dsp_rtcd.h; then
    perl -i -0777 -pe 's/#ifdef ARCH_X86_64\n(CPU_FLAGS get_cpu_flags\(\);)\n#endif/$1/g' SVT-JPEG-XS/Source/Lib/Common/Codec/common_dsp_rtcd.h
fi

# Patch common_dsp_rtcd.c to implement dummy get_cpu_flags
if ! grep -q "CPU_FLAGS get_cpu_flags() {" SVT-JPEG-XS/Source/Lib/Common/Codec/common_dsp_rtcd.c; then
    perl -i -0777 -pe 's/(#endif \/\*ARCH_X86_64\*\/)$/}\n#else\nCPU_FLAGS get_cpu_flags() {\n    return 0;\n}\n$1/g' SVT-JPEG-XS/Source/Lib/Common/Codec/common_dsp_rtcd.c
fi

# Patch decoder_dsp_rtcd.c to guard x86 includes
if ! grep -q "#ifdef ARCH_X86_64" SVT-JPEG-XS/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c; then
    perl -i -0777 -pe 's/(#include "Dwt53Decoder_AVX2.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "Dequant_SSE4.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "NltDec_AVX2.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "UnPack_avx2.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "idwt-avx512.h"\n#include "NltDec_avx512.h"\n#include "Dequant_avx512.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Decoder/Codec/decoder_dsp_rtcd.c
fi

# Patch encoder_dsp_rtcd.c to guard x86 includes
if ! grep -q "#ifdef ARCH_X86_64" SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c; then
    perl -i -0777 -pe 's/(#include "NltEnc_avx2.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "Enc_avx512.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "Dwt_AVX2.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "Quant_sse4_1.h"\n#include "Quant_avx2.h"\n#include "Quant_avx512.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "Pack_avx512.h"\n#include "group_coding_sse4_1.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c
    perl -i -0777 -pe 's/(#include "RateControl_avx2.h")/#ifdef ARCH_X86_64\n$1\n#endif/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/encoder_dsp_rtcd.c
fi

# Patch RateControl.c to use vlc_encode_get_bits
# We need to ensure vlc_encode_get_bits is available for linking by making it non-static in PackPrecinct.h
# if grep -q "static INLINE uint8_t vlc_encode_get_bits" SVT-JPEG-XS/Source/Lib/Encoder/Codec/PackPrecinct.h; then
#     perl -i -pe 's/static INLINE uint8_t vlc_encode_get_bits/uint8_t vlc_encode_get_bits/g' SVT-JPEG-XS/Source/Lib/Encoder/Codec/PackPrecinct.h
# fi


# 3. Build SVT-JPEG-XS
echo -e "${GREEN}Building SVT-JPEG-XS...${NC}"
rm -rf SVT-JPEG-XS/build
mkdir -p SVT-JPEG-XS/build
cd SVT-JPEG-XS/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX="$(pwd)/../../install/svt-jpegxs" -DBUILD_APPS=OFF
make -j$(sysctl -n hw.ncpu)
make install
cd ../..

# 4. Fix SvtJpegxs.pc
echo -e "${GREEN}Fixing SvtJpegxs.pc...${NC}"
# Fix pkgconfig for Qt
# Use local Qt 6.6.3 if available
if [ -d "$PWD/qt6_6/6.6.3/macos" ]; then
    QT_DIR="$PWD/qt6_6/6.6.3/macos"
    echo "Using local Qt 6.6.3 at $QT_DIR"
else
    QT_DIR=$(brew --prefix qt@6 2>/dev/null || brew --prefix qt)
    echo "Using system Qt at $QT_DIR"
fi
PC_FILE="install/svt-jpegxs/lib/pkgconfig/SvtJpegxs.pc"
# Fix prefix (it might be correct now with $(pwd) but good to ensure)
# Fix Cflags to remove /svt-jpegxs suffix
perl -i -pe 's|Cflags: -I\$\{includedir\}/svt-jpegxs|Cflags: -I\$\{includedir\}|g' "$PC_FILE"

# 5. Patch OBS Plugin CMakeLists.txt
echo -e "${GREEN}Patching OBS Plugin CMakeLists.txt...${NC}"
if ! grep -q "link_directories(\${SVTJPEGXS_LIBRARY_DIRS})" obs-jpegxs-plugin/CMakeLists.txt; then
    perl -i -0777 -pe 's/(pkg_check_modules\(SVTJPEGXS SvtJpegxs\))/$1\n    if(SVTJPEGXS_FOUND)\n        link_directories(\${SVTJPEGXS_LIBRARY_DIRS})\n    endif()/g' obs-jpegxs-plugin/CMakeLists.txt
fi

# 6. Build OBS Plugin
echo -e "${GREEN}Building OBS Plugin...${NC}"
export PKG_CONFIG_PATH="$(pwd)/install/svt-jpegxs/lib/pkgconfig:$PKG_CONFIG_PATH"
rm -rf obs-jpegxs-plugin/build
mkdir -p obs-jpegxs-plugin/build
cd obs-jpegxs-plugin/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCPACK_GENERATOR="Bundle" -DCMAKE_PREFIX_PATH="$QT_DIR"
make -j$(sysctl -n hw.ncpu)
make install
cd ../..

# 6.5 Bundle SVT-JPEG-XS libraries
echo -e "${GREEN}Bundling SVT-JPEG-XS libraries...${NC}"
cp -R install/svt-jpegxs/lib/libSvtJpegxs* obs-jpegxs-plugin/build/obs-jpegxs-output.plugin/Contents/MacOS/
cp -R install/svt-jpegxs/lib/libSvtJpegxs* obs-jpegxs-plugin/build/obs-jpegxs-input.plugin/Contents/MacOS/

# 7. Install to OBS
echo -e "${GREEN}Installing to OBS Plugins directory...${NC}"
OBS_PLUGIN_DIR="$HOME/Library/Application Support/obs-studio/plugins"
mkdir -p "$OBS_PLUGIN_DIR"

# Remove old installs if they exist
rm -rf "$OBS_PLUGIN_DIR/obs-jpegxs-output.plugin"
rm -rf "$OBS_PLUGIN_DIR/obs-jpegxs-input.plugin"
rm -rf "$OBS_PLUGIN_DIR/obs-jpegxs-output" # Clean up old dir structure
rm -rf "$OBS_PLUGIN_DIR/obs-jpegxs-input"  # Clean up old dir structure

# Install as .plugin
cp -R obs-jpegxs-plugin/build/obs-jpegxs-output.plugin "$OBS_PLUGIN_DIR/obs-jpegxs-output.plugin"
cp -R obs-jpegxs-plugin/build/obs-jpegxs-input.plugin "$OBS_PLUGIN_DIR/obs-jpegxs-input.plugin"

# Sign the plugins (ad-hoc)
echo -e "${GREEN}Signing plugins...${NC}"
codesign --force --sign - "$OBS_PLUGIN_DIR/obs-jpegxs-output.plugin/Contents/MacOS/libSvtJpegxs.0.dylib"
codesign --force --sign - "$OBS_PLUGIN_DIR/obs-jpegxs-input.plugin/Contents/MacOS/libSvtJpegxs.0.dylib"
codesign --force --deep --sign - "$OBS_PLUGIN_DIR/obs-jpegxs-output.plugin"
codesign --force --deep --sign - "$OBS_PLUGIN_DIR/obs-jpegxs-input.plugin"

echo -e "${GREEN}Installation Complete!${NC}"
echo "Please restart OBS Studio."
