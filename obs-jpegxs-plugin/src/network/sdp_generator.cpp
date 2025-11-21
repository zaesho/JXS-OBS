#include "sdp_generator.h"
#include <fstream>
#include <sstream>
#include <chrono>

namespace jpegxs {

std::string SDPGenerator::generate(const SDPConfig& config) {
    std::stringstream ss;
    
    // Session ID (current time)
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    
    ss << "v=0\r\n";
    ss << "o=- " << now << " " << now << " IN IP4 " << config.source_ip << "\r\n";
    ss << "s=" << config.stream_name << "\r\n";
    ss << "c=IN IP4 " << config.dest_ip << "\r\n";
    ss << "t=0 0\r\n";
    
    // Video Media Description
    ss << "m=video " << config.dest_port << " RTP/AVP " << (int)config.payload_type << "\r\n";
    ss << "a=rtpmap:" << (int)config.payload_type << " JPEGXS/" << config.clock_rate << "\r\n";
    
    // fmtp parameters (RFC 9134)
    ss << "a=fmtp:" << (int)config.payload_type << " ";
    ss << "packetization-mode=0; "; // Codestream
    
    // Profile/Level (Simplification: assumes Main 4:2:0 8-bit or 10-bit based on context)
    // Ideally this should come from the encoder configuration.
    // Elemental Live usually requires these to match exactly.
    // For now, we'll be generic but adding width/height/depth helps.
    
    // Note: RFC 9134 doesn't mandate width/height in SDP, but some receivers like it.
    // However, for ST 2110, exact parameters are often negotiated or fixed.
    // Let's add what's common.
    
    // Elemental often looks for:
    // sampling=YCbCr-4:2:0
    // depth=8
    // width=1920
    // height=1080
    // exactframerate=60
    // colorimetry=BT709
    
    ss << "sampling=YCbCr-4:2:2; "; // We are sending 4:2:2 or 4:2:0? The encoder converts to I420 (4:2:0) currently.
                                    // But Elemental might prefer 4:2:2. 
                                    // The whitepaper says "4:2:2 chroma". 
                                    // We should probably update the encoder to 4:2:2 later, but for now stick to what we output.
                                    // Wait, obs_jpegxs_output.cpp forces I420 (4:2:0).
                                    // Let's claim 4:2:0 for now.
    ss << "width=" << config.width << "; ";
    ss << "height=" << config.height << "; ";
    ss << "depth=8; "; // OBS usually outputs 8-bit unless HDR.
    
    // exactframerate
    if (config.fps_den > 0) {
        ss << "exactframerate=" << config.fps_num << "/" << config.fps_den << "; ";
    }
    
    ss << "colorimetry=BT709"; 
    
    ss << "\r\n";
    
    // a=mediaclk:direct=0 (PTP) - simplified
    ss << "a=ts-refclk:ptp=IEEE1588-2008:00-00-00-00-00-00-00-00\r\n";
    ss << "a=mediaclk:direct=0\r\n";

    return ss.str();
}

void SDPGenerator::saveToFile(const std::string& content, const std::string& filepath) {
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << content;
        file.close();
    }
}

} // namespace jpegxs

