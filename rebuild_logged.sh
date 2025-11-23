#!/bin/bash
echo "Starting build at $(date)" > build_log.txt
cd obs-jpegxs-plugin
mkdir -p build
cd build
cmake .. >> ../../build_log.txt 2>&1
make -j$(sysctl -n hw.ncpu) >> ../../build_log.txt 2>&1
make install >> ../../build_log.txt 2>&1
echo "Finished build at $(date)" >> ../../build_log.txt

