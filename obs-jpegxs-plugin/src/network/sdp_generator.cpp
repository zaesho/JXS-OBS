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
    std::string payload_name = config.use_aws_compatibility ? "jxsv" : "JPEGXS";
    ss << "m=video " << config.dest_port << " RTP/AVP " << (int)config.payload_type << "\r\n";
    ss << "a=rtpmap:" << (int)config.payload_type << " " << payload_name << "/" << config.clock_rate << "\r\n";
    
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
    
    ss << "sampling=" << config.sampling << "; ";
    ss << "width=" << config.width << "; ";
    ss << "height=" << config.height << "; ";
    ss << "depth=" << (int)config.depth << "; ";
    
    // exactframerate
    if (config.fps_den > 0) {
        ss << "exactframerate=" << config.fps_num << "/" << config.fps_den << "; ";
    }
    
    ss << "colorimetry=BT709"; 
    
    if (config.use_aws_compatibility) {
        ss << "; TP=2110TPN; TCS=SDR; PM=2110GPM; SSN=ST2110-22:2018; PAR=1:1";
    }
    
    ss << "\r\n";
    
    // a=mediaclk:direct=0 (PTP) - simplified
    ss << "a=ts-refclk:ptp=IEEE1588-2008:00-00-00-00-00-00-00-00\r\n";
    ss << "a=mediaclk:direct=0\r\n";
    
    // Audio Media Description (ST 2110-30 / AES67)
    if (config.audio_enabled && config.audio_dest_port > 0) {
        std::string audio_fmt = (config.audio_bit_depth == 24) ? "L24" : "L16";
        
        ss << "m=audio " << config.audio_dest_port << " RTP/AVP " << (int)config.audio_payload_type << "\r\n";
        ss << "c=IN IP4 " << config.dest_ip << "\r\n"; // Assume same IP for audio
        ss << "a=rtpmap:" << (int)config.audio_payload_type << " " 
           << audio_fmt << "/" << config.audio_sample_rate << "/" << (int)config.audio_channels << "\r\n";
        ss << "a=ptime:1\r\n"; // 1ms packet time is standard for ST 2110-30 Class A/B/C
        ss << "a=ts-refclk:ptp=IEEE1588-2008:00-00-00-00-00-00-00-00\r\n";
        ss << "a=mediaclk:direct=0\r\n";
    }

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

