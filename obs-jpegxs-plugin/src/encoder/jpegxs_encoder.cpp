/*
 * JPEG XS Encoder Wrapper Implementation
 */

#include "jpegxs_encoder.h"
#include <cstring>
#include <thread>
#include <cstdlib> // For posix_memalign/free
#include <obs-module.h>

// SVT-JPEG-XS encoder API
#include <svt-jpegxs/SvtJpegxsEnc.h>

JpegXSEncoder::JpegXSEncoder()
    : encoder_handle_(nullptr)
    , width_(0)
    , height_(0)
    , fps_num_(0)
    , fps_den_(0)
    , bitrate_mbps_(0.0f)
    , bit_depth_(8)
    , is_444_(false)
    , is_422_(false)
{
    memset(&stats_, 0, sizeof(stats_));
}

JpegXSEncoder::~JpegXSEncoder()
{
    // Cleanup SVT-JPEG-XS encoder
    if (encoder_handle_) {
        svt_jpeg_xs_encoder_api_t *enc_api = static_cast<svt_jpeg_xs_encoder_api_t*>(encoder_handle_);
        svt_jpeg_xs_encoder_close(enc_api);
        delete enc_api;
        encoder_handle_ = nullptr;
    }
    
    // Free aligned buffer if used
    if (aligned_input_buffer_) {
        free(aligned_input_buffer_);
        aligned_input_buffer_ = nullptr;
        aligned_input_size_ = 0;
    }
}

bool JpegXSEncoder::initialize(uint32_t width, uint32_t height,
                               uint32_t fps_num, uint32_t fps_den,
                               float bitrate_mbps, uint32_t threads_num,
                               int bit_depth, bool is_444, bool is_422,
                               int input_bit_depth)
{
    width_ = width;
    height_ = height;
    fps_num_ = fps_num;
    fps_den_ = fps_den;
    bitrate_mbps_ = bitrate_mbps;
    threads_num_ = threads_num;
    bit_depth_ = bit_depth;
    is_444_ = is_444;
    is_422_ = is_422;
    input_bit_depth_ = (input_bit_depth > 0) ? input_bit_depth : bit_depth;
    
    // Initialize SVT-JPEG-XS encoder
    blog(LOG_INFO, "[JpegXSEncoder] Initializing encoder: %ux%u @ %u/%u fps, %d-bit (input %d), 444=%d, 422=%d", 
         width, height, fps_num, fps_den, bit_depth, input_bit_depth_, is_444, is_422);

    svt_jpeg_xs_encoder_api_t *enc_api = new svt_jpeg_xs_encoder_api_t;
    memset(enc_api, 0, sizeof(*enc_api));
    
    // Load default parameters
    blog(LOG_INFO, "[JpegXSEncoder] Loading default parameters...");
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, enc_api);
    
    if (ret != SvtJxsErrorNone) {
        blog(LOG_ERROR, "[JpegXSEncoder] Failed to load default parameters: %d", ret);
        delete enc_api;
        return false;
    }
    
    // Set video format
    enc_api->source_width = width;
    enc_api->source_height = height;
    enc_api->input_bit_depth = bit_depth;
    if (is_444) {
        enc_api->colour_format = COLOUR_FORMAT_PLANAR_YUV444_OR_RGB;
    } else if (is_422) {
        enc_api->colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    } else {
        enc_api->colour_format = COLOUR_FORMAT_PLANAR_YUV420;
    }
    
    // Calculate bits per pixel from bitrate
    float fps = (float)fps_num / fps_den;
    if (fps <= 0.0f) fps = 60.0f; // Safety
    float total_pixels = width * height * fps;
    float bpp = (bitrate_mbps * 1e6f) / total_pixels;
    
    enc_api->bpp_numerator = (uint32_t)(bpp * 100.0f);  // Store as fixed point
    enc_api->bpp_denominator = 100;
    
    blog(LOG_INFO, "[JpegXSEncoder] BPP: %.2f (Num: %u, Den: %u)", bpp, enc_api->bpp_numerator, enc_api->bpp_denominator);

    // Set low-latency parameters
    enc_api->cpu_profile = 0;  // 0 = Low latency
    
    // Restore V=2 to fix grainy noise artifacts (V=1 compresses poorly at 10:1)
    // H=4 offers a good balance of speed and quality (Standard is H=5)
    enc_api->ndecomp_v = 2;    
    enc_api->ndecomp_h = 5;    
    
    // Optimize for M2 Max (8 Performance Cores)
    // Using 8 threads maps perfectly to the P-cores.
    enc_api->threads_num = 8;
    blog(LOG_INFO, "[JpegXSEncoder] 8-Thread Optimized Mode (V=2, H=5)");
    
    enc_api->use_cpu_flags = CPU_FLAGS_ALL;
    
    // Rate Control: Use Slice-based budget (2) for better quality/latency balance
    // Mode 0 (Precinct) is too strict and causes graininess.
    enc_api->rate_control_mode = 2;

    // Use codestream/frame packetization mode (0)
    enc_api->slice_packetization_mode = 0;

    // Reverting Vertical Prediction to 0 due to SvtJxsErrorEncodeFrameError (0x80002035)
    enc_api->coding_vertical_prediction_mode = 0;
    
    // Enable Fast Sign Handling for better efficiency
    enc_api->coding_signs_handling = 1;
    
    // Slice height = 128 (1080 / 128 = ~8.4 slices)
    // This ensures enough work units for all 8 threads to run in parallel.
    // 128 is a multiple of 32 (safe for V=2).
    enc_api->slice_height = 128;
    
    // Initialize encoder instance
    blog(LOG_INFO, "[JpegXSEncoder] Calling svt_jpeg_xs_encoder_init...");
    ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, enc_api);
    
    if (ret != SvtJxsErrorNone) {
        blog(LOG_ERROR, "[JpegXSEncoder] svt_jpeg_xs_encoder_init failed: %d", ret);
        delete enc_api;
        return false;
    }
    blog(LOG_INFO, "[JpegXSEncoder] Encoder initialized successfully");
    
    encoder_handle_ = enc_api;
    
    // Pre-allocate bitstream buffer (estimate 2x uncompressed size to be safe)
    // RGB equivalent size is usually enough, but allow for 10-bit/4:4:4
    size_t buffer_size = width_ * height_ * 8;
    bitstream_buffer_.resize(buffer_size);
    
    return true;
}

bool JpegXSEncoder::encode_frame(uint8_t *yuv_planes[3], uint32_t linesize[3],
                                 uint64_t timestamp,
                                 PacketCallback on_packet)
{
    if (!encoder_handle_) {
        return false;
    }
    
    svt_jpeg_xs_encoder_api_t *enc_api = static_cast<svt_jpeg_xs_encoder_api_t*>(encoder_handle_);
    
    // Prepare input frame
    svt_jpeg_xs_frame_t input_frame;
    memset(&input_frame, 0, sizeof(input_frame));
    
    // Avoid memory copy if possible
    // If 8-bit and not 10-bit, use pointers directly if strides match constraints.
    // SVT-JPEG-XS expects specific buffer layout or pointers.
    // OBS provides planes with padding (linesize).
    
    // For 8-bit, we can pass pointers directly
    // However, if 10-bit requires packing or specific layout, we copy.
    
    if (bit_depth_ > 8) {
        // 10-bit handling (existing logic)
        // Calculate tightly packed sizes (Stride = Width * 2 bytes)
        uint32_t tight_stride_y = width_ * 2;
        uint32_t tight_stride_uv = tight_stride_y; 
        if (!is_444_) tight_stride_uv = width_; // 422/420 chroma width is half -> stride = width bytes
        else tight_stride_uv = width_ * 2; // 444 is full width -> stride = width*2 bytes

        size_t size_y = tight_stride_y * height_;
        size_t size_uv = tight_stride_uv * height_;
        if (!is_444_ && !is_422_) size_uv = tight_stride_uv * (height_ / 2); // 420

        size_t required_buffer_size = size_y + size_uv * 2;
        
        if (!aligned_input_buffer_ || aligned_input_size_ < required_buffer_size) {
            if (aligned_input_buffer_) free(aligned_input_buffer_);
            #ifdef _WIN32
            aligned_input_buffer_ = (uint8_t*)_aligned_malloc(required_buffer_size, 64);
            #else
            posix_memalign((void**)&aligned_input_buffer_, 64, required_buffer_size);
            #endif
            aligned_input_size_ = required_buffer_size;
        }
        
        if (aligned_input_buffer_) {
            uint8_t* dst_y = aligned_input_buffer_;
            uint8_t* dst_u = dst_y + size_y;
            uint8_t* dst_v = dst_u + size_uv;
            
            int shift = (input_bit_depth_ > bit_depth_) ? (input_bit_depth_ - bit_depth_) : 0;
            
            // Copy Y row-by-row
            // TODO: Optimize this copy loop with SIMD or parallel copy if it's a bottleneck
            for(uint32_t y=0; y<height_; y++) {
                const uint8_t* src = yuv_planes[0] + y*linesize[0];
                uint8_t* dst = dst_y + y*tight_stride_y;
                
                if (shift > 0) {
                    const uint16_t* s = (const uint16_t*)src;
                    uint16_t* d = (uint16_t*)dst;
                    for (uint32_t x=0; x<width_; x++) {
                        d[x] = s[x] >> shift;
                    }
                } else {
                    memcpy(dst, src, tight_stride_y);
                }
            }
            
            // Copy U/V row-by-row
            uint32_t h_uv = (!is_444_ && !is_422_) ? height_/2 : height_;
            uint32_t w_uv = (!is_444_) ? width_/2 : width_;
            
            for(uint32_t y=0; y<h_uv; y++) {
                // U
                const uint8_t* src_u = yuv_planes[1] + y*linesize[1];
                uint8_t* dst_u_ptr = dst_u + y*tight_stride_uv;
                
                if (shift > 0) {
                    const uint16_t* s = (const uint16_t*)src_u;
                    uint16_t* d = (uint16_t*)dst_u_ptr;
                    for (uint32_t x=0; x<w_uv; x++) {
                        d[x] = s[x] >> shift;
                    }
                } else {
                    memcpy(dst_u_ptr, src_u, tight_stride_uv);
                }
                
                // V
                const uint8_t* src_v = yuv_planes[2] + y*linesize[2];
                uint8_t* dst_v_ptr = dst_v + y*tight_stride_uv;
                
                if (shift > 0) {
                    const uint16_t* s = (const uint16_t*)src_v;
                    uint16_t* d = (uint16_t*)dst_v_ptr;
                    for (uint32_t x=0; x<w_uv; x++) {
                        d[x] = s[x] >> shift;
                    }
                } else {
                    memcpy(dst_v_ptr, src_v, tight_stride_uv);
                }
            }
            
            input_frame.image.data_yuv[0] = dst_y;
            input_frame.image.data_yuv[1] = dst_u;
            input_frame.image.data_yuv[2] = dst_v;
            
            input_frame.image.stride[0] = tight_stride_y / 2;
            input_frame.image.stride[1] = tight_stride_uv / 2;
            input_frame.image.stride[2] = tight_stride_uv / 2;
            
            input_frame.image.alloc_size[0] = size_y;
            input_frame.image.alloc_size[1] = size_uv;
            input_frame.image.alloc_size[2] = size_uv;
        }
    } else {
        // 8-bit path
        // Direct pointer assignment to avoid copy if possible.
        // SVT-JPEG-XS generally handles strided input.
        input_frame.image.data_yuv[0] = yuv_planes[0];
        input_frame.image.data_yuv[1] = yuv_planes[1];
        input_frame.image.data_yuv[2] = yuv_planes[2];
        
        input_frame.image.stride[0] = linesize[0];
        input_frame.image.stride[1] = linesize[1];
        input_frame.image.stride[2] = linesize[2];
        
        input_frame.image.alloc_size[0] = linesize[0] * height_;
        if (is_444_ || is_422_) {
             input_frame.image.alloc_size[1] = linesize[1] * height_;
             input_frame.image.alloc_size[2] = linesize[2] * height_;
        } else {
             input_frame.image.alloc_size[1] = linesize[1] * (height_ / 2);
             input_frame.image.alloc_size[2] = linesize[2] * (height_ / 2);
        }
    }

    input_frame.user_prv_ctx_ptr = nullptr;
    
    // Pre-allocate bitstream buffer
    size_t required_size = width_ * height_ * 8; 
    if (bitstream_buffer_.size() < required_size) {
        bitstream_buffer_.resize(required_size);
    }
    
    input_frame.bitstream.buffer = bitstream_buffer_.data();
    input_frame.bitstream.allocation_size = bitstream_buffer_.size();
    input_frame.bitstream.used_size = 0;
    
    // Send frame to encoder
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_send_picture(enc_api, &input_frame, 1);
    
    if (ret != SvtJxsErrorNone && ret != SvtJxsErrorNoErrorEmptyQueue) {
        blog(LOG_ERROR, "[JpegXSEncoder] send_picture failed: 0x%x", ret);
        return false;
    }
    
    // Get packets and STREAM IMMEDIATELY via callback
    bool finished_frame = false;
    int packet_count = 0;
    
    while (!finished_frame) {
        svt_jpeg_xs_frame_t output_frame;
        memset(&output_frame, 0, sizeof(output_frame));
        
        // Blocking wait for packet
        ret = svt_jpeg_xs_encoder_get_packet(enc_api, &output_frame, 1);
        
        if (ret == SvtJxsErrorNone) {
            if (output_frame.bitstream.used_size > 0) {
                // Check for overflow
                if (output_frame.bitstream.used_size > bitstream_buffer_.size()) {
                    blog(LOG_ERROR, "[JpegXSEncoder] Packet overflow");
                    return false;
                }
                
                // STREAM PACKET IMMEDIATELY
                on_packet(output_frame.bitstream.buffer, output_frame.bitstream.used_size);
        
                stats_.bytes_encoded += output_frame.bitstream.used_size;
            }
            
            if (output_frame.bitstream.last_packet_in_frame) {
                finished_frame = true;
            }
            
            packet_count++;
            if (packet_count > 10000) { // Sanity limit
                blog(LOG_ERROR, "[JpegXSEncoder] Too many packets >10000");
                break;
            }
        } else if (ret == SvtJxsErrorNoErrorEmptyQueue) {
            if (packet_count > 0) finished_frame = true;
            else {
                // Shouldn't happen if blocking get_packet is used correctly for valid frame
                // But return false just in case
                return false; 
            }
        } else {
            blog(LOG_ERROR, "[JpegXSEncoder] get_packet failed: %d", ret);
            return false;
        }
    }
    
    if (packet_count > 0) {
        stats_.frames_encoded++;
        return true;
    }
    
    return false;
}

// Legacy overload for compatibility
bool JpegXSEncoder::encode_frame(uint8_t *yuv_planes[3], uint32_t linesize[3],
                                 uint64_t timestamp,
                                 uint8_t **output_data, size_t *output_size)
{
    // This legacy method forces buffering, which we want to avoid.
    // But we keep it for compatibility or fallback.
    output_buffer_.clear();
    
    bool res = encode_frame(yuv_planes, linesize, timestamp, 
        [this](const uint8_t* data, size_t size) {
            output_buffer_.insert(output_buffer_.end(), data, data + size);
        });
    
    if (res && !output_buffer_.empty()) {
        *output_data = output_buffer_.data();
        *output_size = output_buffer_.size();
        return true;
    }
    return false;
}

bool JpegXSEncoder::flush(uint8_t **output_data, size_t *output_size)
{
    if (!encoder_handle_) {
        return false;
    }
    
    svt_jpeg_xs_encoder_api_t *enc_api = static_cast<svt_jpeg_xs_encoder_api_t*>(encoder_handle_);
    
    // Try to get any remaining packets
    svt_jpeg_xs_frame_t output_frame;
    memset(&output_frame, 0, sizeof(output_frame));
    
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_get_packet(enc_api, &output_frame, 0);
    
    if (ret == SvtJxsErrorNone && output_frame.bitstream.used_size > 0) {
        output_buffer_.clear();
        output_buffer_.insert(output_buffer_.end(), 
                     output_frame.bitstream.buffer, 
                     output_frame.bitstream.buffer + output_frame.bitstream.used_size);
        
        if (output_data) *output_data = output_buffer_.data();
        if (output_size) *output_size = output_buffer_.size();
        return true;
    }
    
    return false;
}
