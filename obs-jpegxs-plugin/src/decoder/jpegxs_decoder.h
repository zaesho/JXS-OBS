/*
 * JPEG XS Decoder Wrapper
 * Wraps SVT-JPEG-XS decoder with OBS-friendly interface
 */

#pragma once

#include <cstdint>
#include <vector>

namespace jpegxs {

/**
 * JPEG XS Decoder
 * Manages SVT-JPEG-XS decoder instance
 */
class JpegXSDecoder {
public:
    JpegXSDecoder();
    ~JpegXSDecoder();
    
    /**
     * Initialize decoder
     * @param width Frame width (optional, can be auto-detected)
     * @param height Frame height (optional, can be auto-detected)
     * @param threads_num Number of threads (0 = auto)
     * @return true on success
     */
    bool initialize(uint32_t width = 0, uint32_t height = 0, uint32_t threads_num = 0);
    
    /**
     * Decode a JPEG XS frame
     * @param input Encoded JPEG XS data
     * @param yuv_planes Output YUV plane pointers (allocated by caller)
     * @param linesize Output line sizes for each plane
     * @return true on success
     */
    bool decode_frame(const std::vector<uint8_t> &input,
                     uint8_t *yuv_planes[3], uint32_t linesize[3]);
    
    /**
     * Get frame dimensions (after decoding first frame)
     */
    void get_dimensions(uint32_t &width, uint32_t &height) const {
        width = width_;
        height = height_;
    }
    
    // Getters
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    
    /**
     * Get decoder statistics
     */
    struct Stats {
        uint64_t frames_decoded;
        uint64_t bytes_decoded;
        float average_decode_time_ms;
    };
    
    Stats get_stats() const { return stats_; }
    
private:
    // SVT-JPEG-XS decoder handle (opaque pointer)
    void *decoder_handle_;
    
    // Configuration
    uint32_t width_;
    uint32_t height_;
    bool first_frame_;
    
    // Internal persistent buffers to avoid use-after-free in threaded decoder
    std::vector<uint8_t> buffer_y_;
    std::vector<uint8_t> buffer_u_;
    std::vector<uint8_t> buffer_v_;
    
    // Statistics
    Stats stats_;
};

} // namespace jpegxs
