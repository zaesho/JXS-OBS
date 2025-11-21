#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace jpegxs {

/**
 * RTP Packet structure for RFC 9134 (JPEG XS over RTP)
 */
class RTPPacket {
public:
    // RTP Header fields
    struct Header {
        uint8_t version = 2;        // RTP version (always 2)
        bool padding = false;
        bool extension = false;
        uint8_t csrc_count = 0;
        bool marker = false;        // Set on last packet of frame
        uint8_t payload_type = 96;  // Dynamic payload type for JPEG XS
        uint16_t sequence_number = 0;
        uint32_t timestamp = 0;
        uint32_t ssrc = 0;          // Synchronization source identifier
    };

    // JPEG XS specific RTP payload header (RFC 9134)
    struct JPEGXSPayloadHeader {
        uint8_t k = 0;              // K field (reserved, must be 0)
        uint8_t packetization_mode = 1;  // 0=codestream, 1=slice
        uint16_t line_number = 0;   // Line number of first line in packet
        uint16_t line_offset = 0;   // Byte offset within line
        uint16_t slice_height = 16; // Height of slice in lines
    };

    RTPPacket();
    ~RTPPacket();

    // Setters
    void setHeader(const Header& header);
    void setPayloadHeader(const JPEGXSPayloadHeader& payload_header);
    void setPayload(const uint8_t* data, size_t size);
    
    // Getters
    const Header& getHeader() const { return header_; }
    const JPEGXSPayloadHeader& getPayloadHeader() const { return payload_header_; }
    const std::vector<uint8_t>& getPayload() const { return payload_; }
    
    // Serialize to network format
    std::vector<uint8_t> serialize() const;
    
    // Deserialize from network format
    static std::unique_ptr<RTPPacket> deserialize(const uint8_t* data, size_t size);
    
    // Get total packet size
    size_t getTotalSize() const;
    
    // Utility
    static uint32_t generateSSRC();
    
private:
    Header header_;
    JPEGXSPayloadHeader payload_header_;
    std::vector<uint8_t> payload_;
    
    // Serialize header to buffer
    void serializeHeader(uint8_t* buffer) const;
    void serializePayloadHeader(uint8_t* buffer) const;
    
    // Deserialize header from buffer
    static bool deserializeHeader(const uint8_t* buffer, size_t size, Header& header);
    static bool deserializePayloadHeader(const uint8_t* buffer, size_t size, JPEGXSPayloadHeader& payload_header);
};

/**
 * RTP Packetizer for JPEG XS slices
 */
class RTPPacketizer {
public:
    RTPPacketizer(size_t max_payload_size = 1280);
    ~RTPPacketizer();
    
    // Configure packetizer
    void setSSRC(uint32_t ssrc);
    void setPayloadType(uint8_t pt);
    void setSliceHeight(uint16_t height);
    void setMaxPayloadSize(size_t size);  // MTU consideration
    
    // Packetize JPEG XS encoded frame/slice
    std::vector<std::unique_ptr<RTPPacket>> packetize(
        const uint8_t* jpegxs_data,
        size_t data_size,
        uint32_t timestamp,
        bool is_last_slice_in_frame
    );
    
    // Reset sequence number (on stream restart)
    void reset();
    
private:
    uint32_t ssrc_;
    uint8_t payload_type_;
    uint16_t sequence_number_;
    uint16_t slice_height_;
    size_t max_payload_size_;
};

/**
 * RTP Depacketizer for receiving JPEG XS stream
 */
class RTPDepacketizer {
public:
    RTPDepacketizer();
    ~RTPDepacketizer();
    
    // Process incoming RTP packet
    bool processPacket(const uint8_t* data, size_t size);
    
    // Check if complete frame is ready
    bool isFrameReady() const;
    
    // Get assembled frame data
    std::vector<uint8_t> getFrameData();
    
    // Reset state
    void reset();
    
    // Statistics
    struct Stats {
        uint32_t packets_received = 0;
        uint32_t packets_lost = 0;
        uint32_t frames_assembled = 0;
        uint32_t out_of_order_packets = 0;
    };
    
    const Stats& getStats() const { return stats_; }
    
private:
    struct PacketInfo {
        uint16_t sequence_number;
        std::vector<uint8_t> payload;
        bool received;
    };
    
    std::vector<PacketInfo> packet_buffer_;
    uint16_t expected_sequence_;
    uint32_t current_timestamp_;
    bool frame_started_;
    Stats stats_;
    
    void assembleFrame();
};

} // namespace jpegxs
