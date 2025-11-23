#!/bin/bash
set -e

echo "Building OBS JPEG XS Plugin..."

cd obs-jpegxs-plugin
mkdir -p build
cd build

# Configure with CMake
cmake .. -DQT_ROOT="/Applications/OBS.app/Contents/Frameworks" -DCMAKE_PREFIX_PATH="/Applications/OBS.app/Contents/Frameworks" -DCMAKE_INSTALL_PREFIX="$HOME/Library/Application Support/obs-studio/plugins"

# Build
make -j$(sysctl -n hw.ncpu)

# Install
make install

echo "Build and install complete."
