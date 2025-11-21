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
    // sampling? (e.g. "YCbCr-4:2:0")
    // depth? (e.g. 10)
};

class SDPGenerator {
public:
    static std::string generate(const SDPConfig& config);
    static void saveToFile(const std::string& content, const std::string& filepath);
};

} // namespace jpegxs

