cmake_minimum_required(VERSION 3.16)

project(obs-jpegxs-plugin VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Options
option(BUILD_DECODER "Build decoder/receiver plugin" ON)
option(BUILD_ENCODER "Build encoder/sender plugin" ON)

# OBS Studio paths - point to local clone for development
set(OBS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../obs-studio" CACHE PATH "OBS Studio source directory")

# Platform-specific library finding
if(WIN32)
    # Windows: Manual paths for SVT-JPEG-XS and SRT
    set(SVTJPEGXS_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../install/svt-jpegxs/include" CACHE PATH "SVT-JPEG-XS include directory")
    set(SVTJPEGXS_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/../install/svt-jpegxs/lib/SvtJpegxs.lib" CACHE FILEPATH "SVT-JPEG-XS library")
    
    # Find SRT installation
    if(DEFINED ENV{SRT_ROOT})
        set(SRT_ROOT "$ENV{SRT_ROOT}")
    else()
        set(SRT_ROOT "C:/Program Files/libsrt")
    endif()

    find_path(SRT_INCLUDE_DIR srt/srt.h
        HINTS "${SRT_ROOT}/include"
    )
    find_library(SRT_LIBRARY NAMES srt srt_static
        HINTS "${SRT_ROOT}/lib" "${SRT_ROOT}/lib/Release-x64"
    )
    
    # Find OpenSSL libraries (required by SRT on Windows)
    if(DEFINED ENV{OPENSSL_ROOT})
        set(OPENSSL_ROOT "$ENV{OPENSSL_ROOT}")
    else()
        set(OPENSSL_ROOT "C:/Program Files/OpenSSL-Win64")
    endif()

    find_library(OPENSSL_CRYPTO_LIBRARY NAMES libcrypto
        HINTS "${SRT_ROOT}/lib" "${SRT_ROOT}/lib/Release-x64" "${OPENSSL_ROOT}/lib"
    )
    find_library(OPENSSL_SSL_LIBRARY NAMES libssl
        HINTS "${SRT_ROOT}/lib" "${SRT_ROOT}/lib/Release-x64" "${OPENSSL_ROOT}/lib"
    )
    
    if(NOT SRT_INCLUDE_DIR OR NOT SRT_LIBRARY)
        message(FATAL_ERROR "SRT library not found. Please set SRT_ROOT environment variable or install to default location.")
    endif()
    
    message(STATUS "Found SRT: ${SRT_LIBRARY}")
    message(STATUS "SRT include dir: ${SRT_INCLUDE_DIR}")
    message(STATUS "SVT-JPEG-XS include: ${SVTJPEGXS_INCLUDE_DIRS}")
    message(STATUS "SVT-JPEG-XS lib: ${SVTJPEGXS_LIBRARIES}")
    message(STATUS "OpenSSL crypto: ${OPENSSL_CRYPTO_LIBRARY}")
    message(STATUS "OpenSSL SSL: ${OPENSSL_SSL_LIBRARY}")

    # Find Qt6
    if(DEFINED ENV{QT_ROOT})
        set(CMAKE_PREFIX_PATH "$ENV{QT_ROOT}" ${CMAKE_PREFIX_PATH})
    else()
        # Try standard paths or let CMake find it
        list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.5.3/msvc2019_64")
    endif()
    
    set(CMAKE_PREFIX_PATH "/Applications/OBS.app/Contents/Frameworks" ${CMAKE_PREFIX_PATH})
    find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)
    message(STATUS "Found Qt6: ${Qt6_VERSION}")

else()
    # macOS/Linux: Use pkg-config
    find_package(PkgConfig REQUIRED)
    
    # Find SVT-JPEG-XS (optional for development build)
    pkg_check_modules(SVTJPEGXS SvtJpegxs)
    if(SVTJPEGXS_FOUND)
        link_directories(${SVTJPEGXS_LIBRARY_DIRS})
    endif()
    if(NOT SVTJPEGXS_FOUND)
        message(WARNING "SVT-JPEG-XS not found - building skeleton only")
        message(WARNING "The plugin will compile but encoder/decoder will be stubs")
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
    
    # Find Qt6 on macOS/Linux (assuming system install or environment variable)
    # set(CMAKE_PREFIX_PATH "/Applications/OBS.app/Contents/Frameworks" ${CMAKE_PREFIX_PATH})
    find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

    # Previous override removed
endif()

# OBS headers from source tree
set(OBS_INCLUDE_DIRS 
    "${OBS_SOURCE_DIR}/libobs"
    "${OBS_SOURCE_DIR}/frontend/api"
    "${CMAKE_CURRENT_SOURCE_DIR}"  # For obsconfig.h stub
)

# Platform-specific includes
if(WIN32)
    list(APPEND OBS_INCLUDE_DIRS "${OBS_SOURCE_DIR}/deps/w32-pthreads")
elseif(APPLE)
    list(APPEND OBS_INCLUDE_DIRS "/opt/homebrew/include")  # For SIMDE
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
    src/network/udp_socket.cpp
    src/network/udp_socket.h
    src/network/ptp_clock.h
)

# Encoder specific sources
set(ENCODER_ADDITIONAL_SOURCES
    src/network/pacer.cpp
    src/network/pacer.h
    src/network/sdp_generator.cpp
    src/network/sdp_generator.h
)

# Encoder plugin
if(BUILD_ENCODER)
    set(ENCODER_SOURCES
        ${COMMON_SOURCES}
        ${ENCODER_ADDITIONAL_SOURCES}
        src/encoder/jpegxs_encoder.cpp
        src/encoder/jpegxs_encoder.h
        src/encoder/obs_jpegxs_output.cpp
        src/encoder/plugin_main.cpp
        src/ui/jpegxs-dock.cpp
        src/ui/jpegxs-dock.h
    )

    # Enable Qt MOC
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    add_library(obs-jpegxs-output MODULE ${ENCODER_SOURCES})
    
    # Link libraries
    target_link_libraries(obs-jpegxs-output
        ${SVTJPEGXS_LIBRARIES}
        ${SRT_LIBRARY}
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
    )
    
    # Platform-specific linking
    if(WIN32)
        target_link_libraries(obs-jpegxs-output 
            ws2_32
            winmm  # For timeBeginPeriod
            crypt32  # Windows certificate store APIs
            ${OPENSSL_CRYPTO_LIBRARY}
            ${OPENSSL_SSL_LIBRARY}
        )
        # On Windows, use delay-load for obs.dll with proper linking
        # Generate import library from obs.dll at build time
        # Try to find OBS bin path from environment or standard locations
        if(DEFINED ENV{OBS_BIN_DIR})
            set(OBS_BIN_DIR "$ENV{OBS_BIN_DIR}")
        else()
            set(OBS_BIN_DIR "C:/Program Files/obs-studio/bin/64bit")
        endif()
        
        set(OBS_DLL_PATH "${OBS_BIN_DIR}/obs.dll")
        set(OBS_LIB_PATH "${CMAKE_CURRENT_BINARY_DIR}/obs.lib")
        set(OBS_FRONTEND_API_LIB_PATH "${CMAKE_CURRENT_BINARY_DIR}/obs-frontend-api.lib")
        
        # Create a custom target to generate libraries once
        if(NOT TARGET generate_obs_libs)
            add_custom_command(OUTPUT "${OBS_LIB_PATH}" "${OBS_FRONTEND_API_LIB_PATH}"
                COMMAND lib /def:${CMAKE_CURRENT_SOURCE_DIR}/obs.def /out:${OBS_LIB_PATH} /machine:x64
                COMMAND lib /def:${CMAKE_CURRENT_SOURCE_DIR}/obs-frontend-api.def /out:${OBS_FRONTEND_API_LIB_PATH} /machine:x64
                COMMENT "Generating import libraries from .def files"
                VERBATIM
            )
            add_custom_target(generate_obs_libs DEPENDS "${OBS_LIB_PATH}" "${OBS_FRONTEND_API_LIB_PATH}")
        endif()

        add_dependencies(obs-jpegxs-output generate_obs_libs)
        
        target_link_libraries(obs-jpegxs-output "${OBS_LIB_PATH}" "${OBS_FRONTEND_API_LIB_PATH}")

        # Windows: Link with ws2_32 for networking
        target_link_libraries(obs-jpegxs-output ws2_32)
        target_link_options(obs-jpegxs-output PRIVATE /IGNORE:4099)  # Ignore missing PDB warnings
    elseif(APPLE)
        # Set bundle properties for macOS
        set_target_properties(obs-jpegxs-output PROPERTIES
            PREFIX ""
            OUTPUT_NAME "obs-jpegxs-output"
            BUNDLE_EXTENSION "plugin"
            BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ""
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    else()
        set_target_properties(obs-jpegxs-output PROPERTIES
            PREFIX ""
            OUTPUT_NAME "obs-jpegxs-output"
        )
    endif()

    # Installation - macOS plugin path
    if(APPLE)
        install(TARGETS obs-jpegxs-output
            LIBRARY DESTINATION "$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-jpegxs-output.plugin/Contents/MacOS"
            NAMELINK_SKIP
        )
        # Copy the whole bundle structure
        install(CODE "
            execute_process(COMMAND \${CMAKE_COMMAND} -E copy_directory
                \"\${CMAKE_CURRENT_BINARY_DIR}/obs-jpegxs-output.plugin\"
                \"$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-jpegxs-output.plugin\"
            )
        ")
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
    
    # Link libraries
    # Link libraries
    # Link libraries
    target_link_libraries(obs-jpegxs-input
        ${SVTJPEGXS_LIBRARIES}
        ${SRT_LIBRARY}
        Qt6::Core
        Qt6::Widgets
        Qt6::Gui
    )
    
    # Platform-specific linking
    target_compile_definitions(obs-jpegxs-input PRIVATE
        SIMDE_ENABLE_NATIVE_ALIASES
        OBS_PLUGIN_VERSION="${PROJECT_VERSION}"
    )

    # Platform-specific configuration
    if(WIN32)
        target_link_libraries(obs-jpegxs-input 
            ws2_32
            crypt32
            ${OPENSSL_CRYPTO_LIBRARY}
            ${OPENSSL_SSL_LIBRARY}
        )
        
        # Link against generated obs.lib
        set(OBS_LIB_PATH "${CMAKE_CURRENT_BINARY_DIR}/obs.lib")
        target_link_libraries(obs-jpegxs-input "${OBS_LIB_PATH}")
        add_dependencies(obs-jpegxs-input generate_obs_libs)

        target_compile_definitions(obs-jpegxs-input PRIVATE
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
        )

        target_link_options(obs-jpegxs-input PRIVATE /IGNORE:4099)

        set_target_properties(obs-jpegxs-input PROPERTIES
            PREFIX ""
            OUTPUT_NAME "obs-jpegxs-input"
        )
    elseif(APPLE)
        # Set bundle properties for macOS
        set_target_properties(obs-jpegxs-input PROPERTIES
            PREFIX ""
            OUTPUT_NAME "obs-jpegxs-input"
            BUNDLE_EXTENSION "plugin"
            BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ""
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    else()
        set_target_properties(obs-jpegxs-input PROPERTIES
            PREFIX ""
            OUTPUT_NAME "obs-jpegxs-input"
        )
    endif()

    # Installation - macOS plugin path
    if(APPLE)
        install(TARGETS obs-jpegxs-input
            LIBRARY DESTINATION "$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-jpegxs-input.plugin/Contents/MacOS"
            NAMELINK_SKIP
        )
        # Copy the whole bundle structure
        install(CODE "
            execute_process(COMMAND \${CMAKE_COMMAND} -E copy_directory
                \"\${CMAKE_CURRENT_BINARY_DIR}/obs-jpegxs-input.plugin\"
                \"$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-jpegxs-input.plugin\"
            )
        ")
    else()
        install(TARGETS obs-jpegxs-input
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/obs-plugins
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/obs-plugins)
    endif()
endif()

# macOS specific settings
if(APPLE)
    set_target_properties(obs-jpegxs-output PROPERTIES
        INSTALL_RPATH "@loader_path;@loader_path/../../../../../../../../../../Applications/OBS.app/Contents/Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
    if(BUILD_DECODER)
        set_target_properties(obs-jpegxs-input PROPERTIES
            INSTALL_RPATH "@loader_path;@loader_path/../../../../../../../../../../Applications/OBS.app/Contents/Frameworks"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endif()
