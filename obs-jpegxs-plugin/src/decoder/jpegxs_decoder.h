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
     * @param input_data Pointer to encoded JPEG XS data
     * @param input_size Size of encoded data
     * @param yuv_planes Output YUV plane pointers (optional, pass nullptr to use internal buffers)
     * @param linesize Output line sizes for each plane (optional if yuv_planes is nullptr)
     * @return true on success
     */
    bool decode_frame(const uint8_t* input_data, size_t input_size,
                     uint8_t *yuv_planes[3] = nullptr, uint32_t linesize[3] = nullptr);
    
    // Access to internal buffers (valid until next decode)
    const uint8_t* get_y_buffer() const { return buffer_y_; }
    const uint8_t* get_u_buffer() const { return buffer_u_; }
    const uint8_t* get_v_buffer() const { return buffer_v_; }
    
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
    uint8_t getBitDepth() const { return bit_depth_; }
    int getFormat() const { return format_; }
    
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
    uint8_t bit_depth_;
    int format_; // ColourFormat_t
    bool first_frame_;
    
    // Internal persistent buffers to avoid use-after-free in threaded decoder
    // We manage these manually to ensure 64-byte alignment for SVT-JPEG-XS
    uint8_t* buffer_y_ = nullptr;
    uint8_t* buffer_u_ = nullptr;
    uint8_t* buffer_v_ = nullptr;
    size_t buffer_y_size_ = 0;
    size_t buffer_u_size_ = 0;
    size_t buffer_v_size_ = 0;
    
    // Statistics
    Stats stats_;
};

} // namespace jpegxs
