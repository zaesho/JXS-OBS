#include "rtp_packet.h"
#include <cstring>
#include <random>
#include <algorithm>

// Platform-specific network byte order functions
#ifdef _WIN32
    #define NOMINMAX  // Prevent Windows from defining min/max macros
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

namespace jpegxs {

// RTP constants
constexpr size_t RTP_HEADER_SIZE = 12;
constexpr size_t JPEGXS_PAYLOAD_HEADER_SIZE = 8;
constexpr size_t DEFAULT_MAX_PAYLOAD_SIZE = 1280;  // Reduced to be safe for SRT default MSS/Payload limits

// RTPPacket Implementation

RTPPacket::RTPPacket() = default;
RTPPacket::~RTPPacket() = default;

void RTPPacket::setHeader(const Header& header) {
    header_ = header;
}

void RTPPacket::setPayloadHeader(const JPEGXSPayloadHeader& payload_header) {
    payload_header_ = payload_header;
}

void RTPPacket::setPayload(const uint8_t* data, size_t size) {
    payload_.assign(data, data + size);
}

std::vector<uint8_t> RTPPacket::serialize() const {
    size_t total_size = RTP_HEADER_SIZE + JPEGXS_PAYLOAD_HEADER_SIZE + payload_.size();
    std::vector<uint8_t> buffer(total_size);
    
    serializeHeader(buffer.data());
    serializePayloadHeader(buffer.data() + RTP_HEADER_SIZE);
    
    if (!payload_.empty()) {
        std::memcpy(buffer.data() + RTP_HEADER_SIZE + JPEGXS_PAYLOAD_HEADER_SIZE,
                   payload_.data(), payload_.size());
    }
    
    return buffer;
}

void RTPPacket::serializeHeader(uint8_t* buffer) const {
    // Byte 0: V(2), P(1), X(1), CC(4)
    buffer[0] = (header_.version << 6) | 
                (header_.padding ? 0x20 : 0) |
                (header_.extension ? 0x10 : 0) |
                (header_.csrc_count & 0x0F);
    
    // Byte 1: M(1), PT(7)
    buffer[1] = (header_.marker ? 0x80 : 0) | (header_.payload_type & 0x7F);
    
    // Bytes 2-3: Sequence number
    uint16_t seq = htons(header_.sequence_number);
    std::memcpy(buffer + 2, &seq, 2);
    
    // Bytes 4-7: Timestamp
    uint32_t ts = htonl(header_.timestamp);
    std::memcpy(buffer + 4, &ts, 4);
    
    // Bytes 8-11: SSRC
    uint32_t ssrc = htonl(header_.ssrc);
    std::memcpy(buffer + 8, &ssrc, 4);
}

void RTPPacket::serializePayloadHeader(uint8_t* buffer) const {
    // RFC 9134 JPEG XS Payload Header
    // Bytes 0-1: Reserved (K field) and packetization mode
    buffer[0] = 0;  // K field reserved
    buffer[1] = payload_header_.packetization_mode;
    
    // Bytes 2-3: Line number
    uint16_t line_num = htons(payload_header_.line_number);
    std::memcpy(buffer + 2, &line_num, 2);
    
    // Bytes 4-5: Line offset
    uint16_t line_off = htons(payload_header_.line_offset);
    std::memcpy(buffer + 4, &line_off, 2);
    
    // Bytes 6-7: Slice height
    uint16_t slice_h = htons(payload_header_.slice_height);
    std::memcpy(buffer + 6, &slice_h, 2);
}

std::unique_ptr<RTPPacket> RTPPacket::deserialize(const uint8_t* data, size_t size) {
    if (size < RTP_HEADER_SIZE + JPEGXS_PAYLOAD_HEADER_SIZE) {
        return nullptr;  // Packet too small
    }
    
    auto packet = std::make_unique<RTPPacket>();
    
    // Deserialize RTP header
    if (!deserializeHeader(data, size, packet->header_)) {
        return nullptr;
    }
    
    // Deserialize JPEG XS payload header
    if (!deserializePayloadHeader(data + RTP_HEADER_SIZE, 
                                   size - RTP_HEADER_SIZE,
                                   packet->payload_header_)) {
        return nullptr;
    }
    
    // Extract payload
    size_t payload_offset = RTP_HEADER_SIZE + JPEGXS_PAYLOAD_HEADER_SIZE;
    if (size > payload_offset) {
        size_t payload_size = size - payload_offset;
        packet->payload_.assign(data + payload_offset, data + payload_offset + payload_size);
    }
    
    return packet;
}

bool RTPPacket::deserializeHeader(const uint8_t* buffer, size_t size, Header& header) {
    if (size < RTP_HEADER_SIZE) return false;
    
    header.version = (buffer[0] >> 6) & 0x03;
    header.padding = (buffer[0] & 0x20) != 0;
    header.extension = (buffer[0] & 0x10) != 0;
    header.csrc_count = buffer[0] & 0x0F;
    
    header.marker = (buffer[1] & 0x80) != 0;
    header.payload_type = buffer[1] & 0x7F;
    
    uint16_t seq;
    std::memcpy(&seq, buffer + 2, 2);
    header.sequence_number = ntohs(seq);
    
    uint32_t ts;
    std::memcpy(&ts, buffer + 4, 4);
    header.timestamp = ntohl(ts);
    
    uint32_t ssrc;
    std::memcpy(&ssrc, buffer + 8, 4);
    header.ssrc = ntohl(ssrc);
    
    return header.version == 2;  // Must be RTP v2
}

bool RTPPacket::deserializePayloadHeader(const uint8_t* buffer, size_t size, 
                                         JPEGXSPayloadHeader& payload_header) {
    if (size < JPEGXS_PAYLOAD_HEADER_SIZE) return false;
    
    payload_header.k = buffer[0];
    payload_header.packetization_mode = buffer[1];
    
    uint16_t line_num;
    std::memcpy(&line_num, buffer + 2, 2);
    payload_header.line_number = ntohs(line_num);
    
    uint16_t line_off;
    std::memcpy(&line_off, buffer + 4, 2);
    payload_header.line_offset = ntohs(line_off);
    
    uint16_t slice_h;
    std::memcpy(&slice_h, buffer + 6, 2);
    payload_header.slice_height = ntohs(slice_h);
    
    return true;
}

size_t RTPPacket::getTotalSize() const {
    return RTP_HEADER_SIZE + JPEGXS_PAYLOAD_HEADER_SIZE + payload_.size();
}

uint32_t RTPPacket::generateSSRC() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis;
    return dis(gen);
}

// RTPPacketizer Implementation

RTPPacketizer::RTPPacketizer(size_t max_payload_size)
    : ssrc_(RTPPacket::generateSSRC())
    , payload_type_(96)
    , sequence_number_(0)
    , slice_height_(16)
    , max_payload_size_(max_payload_size) {
}

RTPPacketizer::~RTPPacketizer() = default;

void RTPPacketizer::setSSRC(uint32_t ssrc) {
    ssrc_ = ssrc;
}

void RTPPacketizer::setPayloadType(uint8_t pt) {
    payload_type_ = pt;
}

void RTPPacketizer::setSliceHeight(uint16_t height) {
    slice_height_ = height;
}

void RTPPacketizer::setMaxPayloadSize(size_t size) {
    max_payload_size_ = size;
}

void RTPPacketizer::packetize(
    const uint8_t* jpegxs_data,
    size_t data_size,
    uint32_t timestamp,
    bool is_last_slice_in_frame,
    PacketCallback callback) {
    
    // Pre-allocate scratch buffer
    // RTP Header (12) + Payload Header (8) + Max Payload
    size_t max_packet_size = 12 + 8 + max_payload_size_;
    if (scratch_buffer_.size() < max_packet_size) {
        scratch_buffer_.resize(max_packet_size);
    }
    
    uint8_t* buffer = scratch_buffer_.data();
    
    size_t offset = 0;
    // uint16_t line_number = 0; // Unused for now
    
    while (offset < data_size) {
        // RTP Header (12 bytes)
        // V=2, P=0, X=0, CC=0 -> 0x80
        buffer[0] = 0x80;
        
        size_t remaining = data_size - offset;
        size_t payload_size = std::min(remaining, max_payload_size_);
        bool marker = is_last_slice_in_frame && (offset + payload_size >= data_size);
        
        // M=marker, PT=payload_type
        buffer[1] = (marker ? 0x80 : 0x00) | (payload_type_ & 0x7F);
        
        // Sequence Number (Big Endian)
        uint16_t seq = htons(sequence_number_++);
        std::memcpy(buffer + 2, &seq, 2);
        
        // Timestamp (Big Endian)
        uint32_t ts = htonl(timestamp);
        std::memcpy(buffer + 4, &ts, 4);
        
        // SSRC (Big Endian)
        uint32_t ss = htonl(ssrc_);
        std::memcpy(buffer + 8, &ss, 4);
        
        // JPEG XS Payload Header (8 bytes)
        // K=0, Mode=0 (Codestream)
        buffer[12] = 0x00; // K
        buffer[13] = 0x00; // Mode 0
        
        // Line Number = 0 (Codestream mode doesn't use it per packet typically unless Slice mode)
        // Since we are using packetization-mode=0 (Codestream), fields are mostly zero or frame-global
        std::memset(buffer + 14, 0, 6); // Line num, offset, slice height all 0 for now
        
        // Copy Payload
        std::memcpy(buffer + 20, jpegxs_data + offset, payload_size);
        
        // Callback with total packet data
        callback(buffer, 20 + payload_size);
        
        offset += payload_size;
    }
}

void RTPPacketizer::reset() {
    sequence_number_ = 0;
}

// RTPDepacketizer Implementation

RTPDepacketizer::RTPDepacketizer()
    : expected_sequence_(0)
    , current_timestamp_(0)
    , frame_started_(false)
    , discarding_frame_(false)
    , waiting_for_start_(true) {
    // Pre-allocate pool
    packet_pool_.resize(8192); // Enough for 4K frame at 1500 MTU
    for(auto& p : packet_pool_) p.payload.reserve(1500);
    
    pending_packets_.reserve(8192);
    frame_buffer_.reserve(1920 * 1080 * 2); // Conservative max
}

RTPDepacketizer::~RTPDepacketizer() = default;

bool RTPDepacketizer::processPacket(const uint8_t* data, size_t size) {
    // 1. Parse RTP Header directly
    RTPPacket::Header header;
    if (!RTPPacket::deserializeHeader(data, size, header)) {
        return false;
    }
    
    stats_.packets_received++;
    
    // Sync logic: Wait for a frame boundary (Marker) before starting to collect
    if (waiting_for_start_) {
        if (header.marker) {
            waiting_for_start_ = false;
            // The next packet should be the start of a new frame
        }
        return false;
    }
    
    // 2. Parse JPEG XS Payload Header
    // Skip RTP Header (12 bytes)
    size_t offset = 12;
    if (size < offset + 8) return false; // Too small for payload header
    
    offset += 8; // Skip payload header
    
    // Start new frame on timestamp change
    if (!frame_started_ || header.timestamp != current_timestamp_) {
        if (frame_started_) {
            // Previous frame incomplete, discard
            pool_used_ = 0;
            pending_packets_.clear();
        }
        frame_started_ = true;
        discarding_frame_ = false;
        current_timestamp_ = header.timestamp;
    }
    
    // If we are discarding this frame due to previous loss, ignore this packet
    if (discarding_frame_) {
        expected_sequence_ = header.sequence_number + 1;
        return false;
    }
    
    // Check for lost packets
    if (frame_started_ && header.sequence_number != expected_sequence_) {
        int16_t diff = static_cast<int16_t>(header.sequence_number - expected_sequence_);
        
        if (diff > 0) {
            stats_.packets_lost += diff;
            // Loss detected within the frame. Discard entire frame.
            pool_used_ = 0;
            pending_packets_.clear();
            discarding_frame_ = true;
            expected_sequence_ = header.sequence_number + 1;
            return false; 
        } else if (diff < 0) {
             stats_.out_of_order_packets++;
             return false;
        }
    }
    
        // Store packet payload directly into pool
    size_t payload_size = size - offset;
    if (payload_size > 0) {
        if (pool_used_ >= packet_pool_.size()) {
            // Pool exhausted, resize
            // NOTE: Vector resize invalidates pointers, but we use indices now so it's safe!
            size_t old_size = packet_pool_.size();
            packet_pool_.resize(old_size * 2);
            for(size_t i=old_size; i<packet_pool_.size(); ++i) {
                packet_pool_[i].payload.reserve(1500);
            }
        }
        
        size_t idx = pool_used_++;
        PacketData& pdata = packet_pool_[idx];
        pdata.seq = header.sequence_number;
        
        // Copy without re-allocation (if capacity sufficient)
        pdata.payload.assign(data + offset, data + offset + payload_size);
        
        pending_packets_.push_back(idx);
    }
    
    expected_sequence_ = header.sequence_number + 1;
    
    // Check if frame is complete (marker bit set)
    if (header.marker) {
        assembleFrame();
        return true;
    }
    
    return false;
}

void RTPDepacketizer::assembleFrame() {
    // Sort packets by sequence number using indices
    std::sort(pending_packets_.begin(), pending_packets_.end(),
              [this](size_t a_idx, size_t b_idx) {
                  const auto& a = packet_pool_[a_idx];
                  const auto& b = packet_pool_[b_idx];
                  uint16_t diff = a.seq - b.seq;
                  return static_cast<int16_t>(diff) < 0;
              });
              
    // Flatten
    frame_buffer_.clear();
    size_t total_size = 0;
    for (size_t idx : pending_packets_) {
        total_size += packet_pool_[idx].payload.size();
    }
    
    if (frame_buffer_.capacity() < total_size) {
        frame_buffer_.reserve(total_size * 2); // Grow aggressively
    }
    
    for (size_t idx : pending_packets_) {
        const auto& p = packet_pool_[idx];
        frame_buffer_.insert(frame_buffer_.end(), p.payload.begin(), p.payload.end());
    }
    
    // Reset pool usage
    pool_used_ = 0;
    pending_packets_.clear();
    
    stats_.frames_assembled++;
    frame_started_ = false;
}

bool RTPDepacketizer::isFrameReady() const {
    return !frame_buffer_.empty() && !frame_started_;
}

const uint8_t* RTPDepacketizer::getFrameData(size_t& size) const {
    size = frame_buffer_.size();
    return frame_buffer_.data();
}

void RTPDepacketizer::reset() {
    pending_packets_.clear();
    frame_buffer_.clear();
    expected_sequence_ = 0;
    current_timestamp_ = 0;
    frame_started_ = false;
    discarding_frame_ = false;
    waiting_for_start_ = true;
    stats_ = Stats();
}

} // namespace jpegxs
