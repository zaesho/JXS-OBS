#pragma once

#include <string>
#include <vector>

namespace jpegxs {

struct SDPConfig {
    std::string stream_name = "OBS JPEG XS Stream";
    std::string source_ip = "0.0.0.0";
    std::string dest_ip;
    uint16_t dest_port;
    
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    
    uint8_t payload_type = 96;
    uint32_t clock_rate = 90000;
    
    // JPEG XS specifics
    // profile-level-id? 
    // packetization-mode?
    std::string sampling = "YCbCr-4:2:0"; // "YCbCr-4:2:0" or "YCbCr-4:4:4"
    uint8_t depth = 8;
    
    bool use_aws_compatibility = false; // Enables jxsv payload and extra attributes
    
    // Audio Configuration
    bool audio_enabled = false;
    uint16_t audio_dest_port = 0;
    uint8_t audio_channels = 2;
    uint8_t audio_bit_depth = 16; // 16 or 24
    uint32_t audio_sample_rate = 48000;
    uint8_t audio_payload_type = 97;
};

class SDPGenerator {
public:
    static std::string generate(const SDPConfig& config);
    static void saveToFile(const std::string& content, const std::string& filepath);
};

} // namespace jpegxs

