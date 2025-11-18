cmake_minimum_required(VERSION 3.16)

project(obs-jpegxs-plugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Options
option(BUILD_DECODER "Build decoder/receiver plugin" ON)
option(BUILD_ENCODER "Build encoder/sender plugin" ON)

# OBS Studio paths - point to local clone for development
set(OBS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../obs-studio" CACHE PATH "OBS Studio source directory")

# Find required packages
find_package(PkgConfig REQUIRED)

# Find SVT-JPEG-XS (optional for development build)
pkg_check_modules(SVTJPEGXS SvtJpegxs)
if(NOT SVTJPEGXS_FOUND)
    message(WARNING "SVT-JPEG-XS not found - building skeleton only")
    message(WARNING "The plugin will compile but encoder/decoder will be stubs")
    message(WARNING "For production builds, compile SVT-JPEG-XS on x86_64 Linux")
    set(SVTJPEGXS_INCLUDE_DIRS "")
    set(SVTJPEGXS_LIBRARIES "")
    add_definitions(-DSTUB_JPEGXS=1)
endif()

# Find SRT (Homebrew installation)
find_path(SRT_INCLUDE_DIR srt/srt.h
    HINTS /opt/homebrew/include /usr/local/include
)
find_library(SRT_LIBRARY NAMES srt
    HINTS /opt/homebrew/lib /usr/local/lib
)

if(NOT SRT_INCLUDE_DIR OR NOT SRT_LIBRARY)
    message(FATAL_ERROR "SRT library not found. Please install with: brew install srt")
endif()

message(STATUS "Found SRT: ${SRT_LIBRARY}")
message(STATUS "SRT include dir: ${SRT_INCLUDE_DIR}")

# OBS headers from source tree
set(OBS_INCLUDE_DIRS 
    "${OBS_SOURCE_DIR}/libobs"
    "${CMAKE_CURRENT_SOURCE_DIR}"  # For obsconfig.h stub
    "/opt/homebrew/include"  # For SIMDE
)

# Add platform-specific headers (w32-pthreads is Windows-only)
if(WIN32)
    list(APPEND OBS_INCLUDE_DIRS "${OBS_SOURCE_DIR}/deps/w32-pthreads")
endif()

# Include directories
include_directories(
    ${OBS_INCLUDE_DIRS}
    ${SVTJPEGXS_INCLUDE_DIRS}
    ${SRT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Common source files
set(COMMON_SOURCES
    src/network/rtp_packet.cpp
    src/network/rtp_packet.h
    src/network/srt_transport.cpp
    src/network/srt_transport.h
)

# Encoder plugin
if(BUILD_ENCODER)
    set(ENCODER_SOURCES
        ${COMMON_SOURCES}
        src/encoder/jpegxs_encoder.cpp
        src/encoder/jpegxs_encoder.h
        src/encoder/obs_jpegxs_output.cpp
        src/encoder/plugin_main.cpp
    )

    add_library(obs-jpegxs-output MODULE ${ENCODER_SOURCES})
    
    # OBS plugins don't link to libobs - symbols resolved at runtime
    target_link_libraries(obs-jpegxs-output
        ${SVTJPEGXS_LIBRARIES}
        ${SRT_LIBRARY}
    )

    target_compile_definitions(obs-jpegxs-output PRIVATE
        OBS_PLUGIN_VERSION="${PROJECT_VERSION}"
    )

    # Set bundle properties for macOS
    set_target_properties(obs-jpegxs-output PROPERTIES
        PREFIX ""
        OUTPUT_NAME "obs-jpegxs-output"
        SUFFIX ".so"
        BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ""
    )
    
    # Allow undefined symbols (resolved by OBS at load time)
    if(APPLE)
        set_target_properties(obs-jpegxs-output PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    endif()

    # Installation - macOS plugin path
    if(APPLE)
        install(TARGETS obs-jpegxs-output
            LIBRARY DESTINATION "$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-jpegxs-output/bin")
    else()
        install(TARGETS obs-jpegxs-output
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/obs-plugins)
    endif()
endif()

# Decoder plugin
if(BUILD_DECODER)
    set(DECODER_SOURCES
        ${COMMON_SOURCES}
        src/decoder/jpegxs_decoder.cpp
        src/decoder/jpegxs_decoder.h
        src/decoder/obs_jpegxs_source.cpp
        src/decoder/plugin_main.cpp
    )

    add_library(obs-jpegxs-input MODULE ${DECODER_SOURCES})
    
    # OBS plugins don't link to libobs - symbols resolved at runtime
    target_link_libraries(obs-jpegxs-input
        ${SVTJPEGXS_LIBRARIES}
        ${SRT_LIBRARY}
    )

    target_compile_definitions(obs-jpegxs-input PRIVATE
        OBS_PLUGIN_VERSION="${PROJECT_VERSION}"
    )

    # Set bundle properties for macOS
    set_target_properties(obs-jpegxs-input PROPERTIES
        PREFIX ""
        OUTPUT_NAME "obs-jpegxs-input"
        SUFFIX ".so"
        BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ""
    )
    
    # Allow undefined symbols (resolved by OBS at load time)
    if(APPLE)
        set_target_properties(obs-jpegxs-input PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    endif()

    # Installation - macOS plugin path
    if(APPLE)
        install(TARGETS obs-jpegxs-input
            LIBRARY DESTINATION "$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-jpegxs-input/bin")
    else()
        install(TARGETS obs-jpegxs-input
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/obs-plugins)
    endif()
endif()

# macOS specific settings
if(APPLE)
    set_target_properties(obs-jpegxs-output PROPERTIES
        INSTALL_RPATH "@loader_path/"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    if(BUILD_DECODER)
        set_target_properties(obs-jpegxs-input PROPERTIES
            INSTALL_RPATH "@loader_path/"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endif()
