/*
 * JPEG XS Encoder Wrapper
 * Wraps SVT-JPEG-XS encoder with OBS-friendly interface
 */

#pragma once

#include <cstdint>
#include <vector>

/**
 * JPEG XS Encoder
 * Manages SVT-JPEG-XS encoder instance with low-latency configuration
 */
class JpegXSEncoder {
public:
    JpegXSEncoder();
    ~JpegXSEncoder();
    
    /**
     * Initialize encoder
     * @param width Frame width
     * @param height Frame height
     * @param fps_num Frame rate numerator
     * @param fps_den Frame rate denominator
     * @param bitrate_mbps Target bitrate in Mbps
     * @return true on success
     */
    bool initialize(uint32_t width, uint32_t height, 
                   uint32_t fps_num, uint32_t fps_den,
                   float bitrate_mbps, uint32_t threads_num = 0);
    
    /**
     * Encode a video frame
     * @param yuv_planes Array of YUV plane pointers
     * @param linesize Array of line sizes for each plane
     * @param timestamp Frame timestamp in nanoseconds
     * @param output Output buffer for encoded data
     * @return true on success
     */
    bool encode_frame(uint8_t *yuv_planes[3], uint32_t linesize[3],
                     uint64_t timestamp,
                     std::vector<uint8_t> &output);
    
    /**
     * Flush encoder and get any remaining packets
     * @param output Output buffer for encoded data
     * @return true if packets were flushed
     */
    bool flush(std::vector<uint8_t> &output);
    
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
    
    // Reusable buffer for bitstream
    std::vector<uint8_t> bitstream_buffer_;
    
    // Statistics
    Stats stats_;
};
