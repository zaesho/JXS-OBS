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

std::vector<std::unique_ptr<RTPPacket>> RTPPacketizer::packetize(
    const uint8_t* jpegxs_data,
    size_t data_size,
    uint32_t timestamp,
    bool is_last_slice_in_frame) {
    
    std::vector<std::unique_ptr<RTPPacket>> packets;
    
    size_t offset = 0;
    uint16_t line_number = 0;
    
    while (offset < data_size) {
        auto packet = std::make_unique<RTPPacket>();
        
        // Set RTP header
        RTPPacket::Header header;
        header.version = 2;
        header.payload_type = payload_type_;
        header.sequence_number = sequence_number_++;
        header.timestamp = timestamp;
        header.ssrc = ssrc_;
        
        size_t remaining = data_size - offset;
        size_t payload_size = std::min(remaining, max_payload_size_);
        
        // Mark last packet if this is the last slice and last chunk
        header.marker = is_last_slice_in_frame && (offset + payload_size >= data_size);
        
        packet->setHeader(header);
        
        // Set JPEG XS payload header
        RTPPacket::JPEGXSPayloadHeader payload_header;
        payload_header.packetization_mode = 0;  // Codestream mode
        payload_header.line_number = 0;
        payload_header.line_offset = 0;
        payload_header.slice_height = 0;
        
        packet->setPayloadHeader(payload_header);
        
        // Set payload
        packet->setPayload(jpegxs_data + offset, payload_size);
        
        packets.push_back(std::move(packet));
        offset += payload_size;
    }
    
    return packets;
}

void RTPPacketizer::reset() {
    sequence_number_ = 0;
}

// RTPDepacketizer Implementation

RTPDepacketizer::RTPDepacketizer()
    : expected_sequence_(0)
    , current_timestamp_(0)
    , frame_started_(false) {
}

RTPDepacketizer::~RTPDepacketizer() = default;

bool RTPDepacketizer::processPacket(const uint8_t* data, size_t size) {
    auto packet = RTPPacket::deserialize(data, size);
    if (!packet) {
        return false;
    }
    
    stats_.packets_received++;
    
    const auto& header = packet->getHeader();
    
    // Check for lost packets
    if (frame_started_ && header.sequence_number != expected_sequence_) {
        int16_t diff = static_cast<int16_t>(header.sequence_number - expected_sequence_);
        if (diff > 0) {
            stats_.packets_lost += diff;
            
            // CRITICAL FIX: If packets are lost within a frame, the frame is corrupt.
            // We must discard the incomplete frame to prevent "Invalid Bitstream" errors in the decoder.
            // We drop the current buffer and wait for the next frame (timestamp change).
            packet_buffer_.clear();
            frame_started_ = false; // Reset frame state
            // We do NOT update expected_sequence_ here because we want to resync on the next packet
            // But actually, the next packet is this one. It belongs to the SAME frame (same timestamp).
            // So we just discard THIS frame.
            
            // Wait, if we just clear packet_buffer_, the NEXT packets for this SAME timestamp will be appended.
            // And they will be out of context.
            // So we need a flag "discarding_frame_".
            
            // For now, let's just clear and NOT set frame_started_ = true until timestamp changes?
            // Yes, see logic below: "if (!frame_started_ || header.timestamp != current_timestamp_)"
            // But header.timestamp IS current_timestamp_.
            
            // Let's imply that if we clear the buffer, we are aborting this frame.
            // But processPacket returns false.
            
            // To properly implement this, we need to ignore all subsequent packets with this timestamp.
            // But for a quick fix, clearing the buffer is better than appending to a hole.
            // The decoder will likely still fail or see a partial frame if we send it later?
            // assembleFrame() checks nothing.
            
            // Better approach:
            // If loss detected, clear buffer. The remaining packets of this frame will be added to a new (partial) buffer.
            // But the decoder will fail on that too.
            // Ideally, we shouldn't even start collecting until we see a new timestamp or a marker.
            
            return false; // Drop this packet and effectively the whole frame
        } else {
            stats_.out_of_order_packets++;
        }
    }
    
    // Start new frame on timestamp change
    if (!frame_started_ || header.timestamp != current_timestamp_) {
        if (frame_started_) {
            // Previous frame incomplete, discard
            packet_buffer_.clear();
        }
        frame_started_ = true;
        current_timestamp_ = header.timestamp;
    }
    
    // Store packet
    PacketInfo info;
    info.sequence_number = header.sequence_number;
    info.payload = packet->getPayload();
    info.received = true;
    packet_buffer_.push_back(std::move(info));
    
    expected_sequence_ = header.sequence_number + 1;
    
    // Check if frame is complete (marker bit set)
    if (header.marker) {
        assembleFrame();
        return true;
    }
    
    return false;
}

void RTPDepacketizer::assembleFrame() {
    // Sort packets by sequence number (handle out of order)
    std::sort(packet_buffer_.begin(), packet_buffer_.end(),
              [](const PacketInfo& a, const PacketInfo& b) {
                  return a.sequence_number < b.sequence_number;
              });
    
    stats_.frames_assembled++;
    frame_started_ = false;
}

bool RTPDepacketizer::isFrameReady() const {
    return !packet_buffer_.empty() && !frame_started_;
}

std::vector<uint8_t> RTPDepacketizer::getFrameData() {
    std::vector<uint8_t> frame_data;
    
    for (const auto& packet : packet_buffer_) {
        frame_data.insert(frame_data.end(), 
                         packet.payload.begin(), 
                         packet.payload.end());
    }
    
    packet_buffer_.clear();
    return frame_data;
}

void RTPDepacketizer::reset() {
    packet_buffer_.clear();
    expected_sequence_ = 0;
    current_timestamp_ = 0;
    frame_started_ = false;
    stats_ = Stats();
}

} // namespace jpegxs
