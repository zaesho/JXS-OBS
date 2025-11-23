/*
 * JPEG XS Encoder Wrapper
 * Wraps SVT-JPEG-XS encoder with OBS-friendly interface
 */

#pragma once

#include <cstdint>
#include <vector>

#include <functional>

/**
 * JPEG XS Encoder
 * Manages SVT-JPEG-XS encoder instance with low-latency configuration
 */
class JpegXSEncoder {
public:
    using PacketCallback = std::function<void(const uint8_t* data, size_t size)>;

    JpegXSEncoder();
    ~JpegXSEncoder();
    
    /**
     * Initialize encoder
     * @param width Frame width
     * @param height Frame height
     * @param fps_num Frame rate numerator
     * @param fps_den Frame rate denominator
     * @param bitrate_mbps Target bitrate in Mbps
     * @param threads_num Number of threads (0 = auto)
     * @param bit_depth Input bit depth (8 or 10)
     * @param is_444 True for 4:4:4 chroma, false for 4:2:0
     * @return true on success
     */
    bool initialize(uint32_t width, uint32_t height, 
                   uint32_t fps_num, uint32_t fps_den,
                   float bitrate_mbps, uint32_t threads_num = 0,
                   int bit_depth = 8, bool is_444 = false, bool is_422 = false,
                   int input_bit_depth = 0);
    
    /**
     * Encode a video frame and stream packets immediately via callback.
     * @param yuv_planes Array of YUV plane pointers
     * @param linesize Array of line sizes for each plane
     * @param timestamp Frame timestamp in nanoseconds
     * @param on_packet Callback function to send packets as they are produced (Zero-Copy Stream)
     * @return true on success
     */
    bool encode_frame(uint8_t *yuv_planes[3], uint32_t linesize[3],
                     uint64_t timestamp,
                     PacketCallback on_packet);
    
    // Legacy buffer-based encode (deprecated for low latency)
    bool encode_frame(uint8_t *yuv_planes[3], uint32_t linesize[3],
                     uint64_t timestamp,
                     uint8_t **output_data, size_t *output_size);
    
    /**
     * Flush encoder and get any remaining packets
     * @param output_data Pointer to output data
     * @param output_size Size of output data
     * @return true if packets were flushed
     */
    bool flush(uint8_t **output_data, size_t *output_size);
    
    /**
     * Get encoder statistics
     */
    struct Stats {
        uint64_t frames_encoded;
        uint64_t bytes_encoded;
        float average_encode_time_ms;
    };
    
    Stats get_stats() const { return stats_; }
    
private:
    // SVT-JPEG-XS encoder handle (opaque pointer)
    void *encoder_handle_;
    
    // Configuration
    uint32_t width_;
    uint32_t height_;
    uint32_t fps_num_;
    uint32_t fps_den_;
    float bitrate_mbps_;
    uint32_t threads_num_;
    int bit_depth_;
    int input_bit_depth_;
    bool is_444_;
    bool is_422_;
    
    // Reusable buffer for bitstream (passed to encoder)
    std::vector<uint8_t> bitstream_buffer_;
    
    // Internal aligned buffer for 10-bit input (if needed)
    uint8_t* aligned_input_buffer_ = nullptr;
    size_t aligned_input_size_ = 0;
    
    // Reusable buffer for assembled output (returned to user)
    std::vector<uint8_t> output_buffer_;
    
    // Statistics
    Stats stats_;
};
