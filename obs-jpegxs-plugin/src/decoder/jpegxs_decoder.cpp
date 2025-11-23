/*
 * JPEG XS Decoder Wrapper Implementation
 */

#include "jpegxs_decoder.h"
#include <cstring>
#include <cstdlib> // posix_memalign
#include <obs-module.h>

// SVT-JPEG-XS decoder API
#include <svt-jpegxs/SvtJpegxsDec.h>

namespace jpegxs {

JpegXSDecoder::JpegXSDecoder()
    : decoder_handle_(nullptr)
    , width_(0)
    , height_(0)
    , bit_depth_(8)
    , format_(2) // Default to 4:2:0
    , first_frame_(true)
{
    memset(&stats_, 0, sizeof(stats_));
}

JpegXSDecoder::~JpegXSDecoder()
{
    // Cleanup SVT-JPEG-XS decoder
    if (decoder_handle_) {
        svt_jpeg_xs_decoder_api_t *dec_api = static_cast<svt_jpeg_xs_decoder_api_t*>(decoder_handle_);
        svt_jpeg_xs_decoder_close(dec_api);
        delete dec_api;
        decoder_handle_ = nullptr;
    }
    
    if (buffer_y_) free(buffer_y_);
    if (buffer_u_) free(buffer_u_);
    if (buffer_v_) free(buffer_v_);
}

bool JpegXSDecoder::initialize(uint32_t width, uint32_t height, uint32_t threads_num)
{
    width_ = width;
    height_ = height;
    
    // Initialize SVT-JPEG-XS decoder
    svt_jpeg_xs_decoder_api_t *dec_api = new svt_jpeg_xs_decoder_api_t;
    memset(dec_api, 0, sizeof(svt_jpeg_xs_decoder_api_t));
    
    // Set decoder parameters
    dec_api->use_cpu_flags = CPU_FLAGS_ALL;
    dec_api->threads_num = (threads_num > 0) ? threads_num : 4;
    dec_api->packetization_mode = 0;  // Frame-based mode (since we reassemble the full frame before decoding)
    dec_api->proxy_mode = proxy_mode_full;  // Full resolution
    dec_api->verbose = VERBOSE_ERRORS;
    
    decoder_handle_ = dec_api;
    
    // Note: Full initialization happens on first frame decode
    // when we have bitstream to parse
    
    return true;
}

bool JpegXSDecoder::decode_frame(const uint8_t* input_data, size_t input_size,
                                 uint8_t *yuv_planes[3], uint32_t linesize[3])
{
    if (!decoder_handle_) {
        return false;
    }
    
    svt_jpeg_xs_decoder_api_t *dec_api = static_cast<svt_jpeg_xs_decoder_api_t*>(decoder_handle_);
    
    // Check if decoder is initialized (has parsed first frame)
    if (first_frame_ && dec_api->private_ptr == nullptr) {
        // DEBUG: Log first 8 bytes of received bitstream
        if (input_size >= 8) {
            const uint8_t* b = input_data;
            blog(LOG_INFO, "[JpegXSDecoder] Init Frame Bytes: %02X %02X %02X %02X %02X %02X %02X %02X (Size: %zu)", 
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], input_size);
        } else {
            blog(LOG_ERROR, "[JpegXSDecoder] Received bitstream too small: %zu bytes", input_size);
        }

        // Initialize decoder with first frame bitstream
        svt_jpeg_xs_image_config_t image_config;
        SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(
            SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
            dec_api, input_data, input_size, &image_config);
        
        if (ret != SvtJxsErrorNone) {
            blog(LOG_ERROR, "[JpegXSDecoder] decoder_init failed with error 0x%x", ret);
            return false;
        }
        
        // FORCE colour_format to match what we expect (YUV420) if not set correctly
        // SVT-JPEG-XS might default to something else if not specified in codestream?
        // But decoder_init reads it from codestream.
        
        // Update dimensions from bitstream
        width_ = image_config.width;
        height_ = image_config.height;
        bit_depth_ = image_config.bit_depth;
        format_ = image_config.format;
        first_frame_ = false;
        
        blog(LOG_INFO, "[JpegXSDecoder] Initialized: %ux%u, %u bits, Format: %d", 
             width_, height_, bit_depth_, format_);
        
        // Resize internal persistent buffers
        size_t pixel_size = (bit_depth_ > 8) ? 2 : 1;
        size_t luma_size = width_ * height_ * pixel_size;
        size_t chroma_size = luma_size; // Start with 4:4:4 assumption (safe max)
        
        if (format_ == 2) { // COLOUR_FORMAT_PLANAR_YUV420
            chroma_size = luma_size / 4;
        } else if (format_ == 3) { // COLOUR_FORMAT_PLANAR_YUV422
            chroma_size = luma_size / 2;
        } 
        // Format 4 is 4:4:4, so chroma_size == luma_size
        
        // Reallocate aligned buffers
        if (buffer_y_) free(buffer_y_);
        if (buffer_u_) free(buffer_u_);
        if (buffer_v_) free(buffer_v_);
        
        #ifdef _WIN32
        buffer_y_ = (uint8_t*)_aligned_malloc(luma_size, 64);
        buffer_u_ = (uint8_t*)_aligned_malloc(chroma_size, 64);
        buffer_v_ = (uint8_t*)_aligned_malloc(chroma_size, 64);
        #else
        posix_memalign((void**)&buffer_y_, 64, luma_size);
        posix_memalign((void**)&buffer_u_, 64, chroma_size);
        posix_memalign((void**)&buffer_v_, 64, chroma_size);
        #endif
        
        buffer_y_size_ = luma_size;
        buffer_u_size_ = chroma_size;
        buffer_v_size_ = chroma_size;
        
        // Initialize chroma to neutral grey (128)
        // For 10-bit, 512 (0x0200) -> 0x00 0x02 in LE
        // memset sets bytes. 0x80 is valid for 8-bit.
        // For 10-bit, 0x8080 is ~32896 which is out of range (valid 0-1023).
        // But uninitialized is worse. Let's just memset 0 for now to be safe.
        memset(buffer_u_, 0, chroma_size);
        memset(buffer_v_, 0, chroma_size);
    }
    
    // Prepare input frame
    svt_jpeg_xs_frame_t input_frame;
    memset(&input_frame, 0, sizeof(input_frame));
    
    input_frame.bitstream.buffer = const_cast<uint8_t*>(input_data);
    input_frame.bitstream.allocation_size = input_size;
    input_frame.bitstream.used_size = input_size;
    
    // Use internal persistent buffers for the decoder to write into.
    input_frame.image.data_yuv[0] = buffer_y_;
    input_frame.image.data_yuv[1] = buffer_u_;
    input_frame.image.data_yuv[2] = buffer_v_;
    
    // Calculate strides for internal buffers
    size_t pixel_size_frame = (bit_depth_ > 8) ? 2 : 1;
    uint32_t stride_y = width_ * pixel_size_frame;
    uint32_t stride_uv = stride_y;
    
    if (format_ == 2 || format_ == 3) { // 4:2:0 or 4:2:2
        stride_uv = stride_y / 2;
    }
    
    // Stride for SVT-JPEG-XS decoder must be in ELEMENTS if > 8 bit
    uint32_t svt_stride_y = stride_y;
    uint32_t svt_stride_uv = stride_uv;
    
    if (bit_depth_ > 8) {
        svt_stride_y /= 2;
        svt_stride_uv /= 2;
    }
    
    input_frame.image.stride[0] = svt_stride_y;
    input_frame.image.stride[1] = svt_stride_uv;
    input_frame.image.stride[2] = svt_stride_uv;
    input_frame.image.alloc_size[0] = buffer_y_size_;
    input_frame.image.alloc_size[1] = buffer_u_size_;
    input_frame.image.alloc_size[2] = buffer_v_size_;
    
    // Send frame to decoder (frame-based mode)
    // blocking_flag = 1 ensures we wait for the frame to be accepted/processed
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_send_frame(dec_api, &input_frame, 1);
    
    if (ret != SvtJxsErrorNone) {
        blog(LOG_ERROR, "[JpegXSDecoder] send_frame failed with error 0x%x. Debug: w=%u h=%u stride={%u,%u,%u} alloc={%u,%u,%u}", 
             ret, width_, height_, 
             input_frame.image.stride[0], input_frame.image.stride[1], input_frame.image.stride[2],
             input_frame.image.alloc_size[0], input_frame.image.alloc_size[1], input_frame.image.alloc_size[2]);
        
        // If config change, we need to reinit. The error code might be SvtJxsErrorDecoderConfigChange
        // but send_frame documentation doesn't explicitly list it as a return for send_frame,
        // but let's check anyway or handle it if it returns generic error.
        
        if (ret == SvtJxsErrorDecoderConfigChange) {
            blog(LOG_WARNING, "[JpegXSDecoder] Config change detected in send_frame, attempting reinit");
            // Reset first_frame flag to trigger reinit on next call or try now?
            // Simplest is to return false and let next frame trigger reinit if we reset the flag.
            // But we need to close/open or reinit.
            // Let's try to set private_ptr to null and return false so next frame re-inits.
            dec_api->private_ptr = nullptr; 
            first_frame_ = true;
            return false;
        }
        
        blog(LOG_ERROR, "[JpegXSDecoder] send_frame failed with error 0x%x", ret);
        return false;
    }
    
    // Get decoded frame
    svt_jpeg_xs_frame_t output_frame;
    memset(&output_frame, 0, sizeof(output_frame));
    
    ret = svt_jpeg_xs_decoder_get_frame(dec_api, &output_frame, 1);  // blocking
    
    if (ret == SvtJxsErrorNone) {
        // Frame decoded successfully
        
        // ALWAYS copy from our internal persistent buffers to the caller's provided buffers
        // This handles both cases:
        // 1. Decoder wrote to our persistent buffers (as requested in send_frame)
        // 2. Decoder returned its own buffers (we ignore them, but we should have used persistent ones)
        //    Actually, if decoder returned its own buffers, we should copy FROM them.
        
        // Let's check what we got back
        uint8_t* src_y = static_cast<uint8_t*>(output_frame.image.data_yuv[0]);
        uint8_t* src_u = static_cast<uint8_t*>(output_frame.image.data_yuv[1]);
        uint8_t* src_v = static_cast<uint8_t*>(output_frame.image.data_yuv[2]);
        
        // If decoder returned null pointers, assume it wrote to the buffers we provided in send_frame
        if (!src_y) src_y = buffer_y_;
        if (!src_u) src_u = buffer_u_;
        if (!src_v) src_v = buffer_v_;
        
        // Check if we need to update our internal buffers from the decoder's output
        // This happens if the decoder ignored our provided buffers and allocated its own,
        // AND the caller expects the result in our internal buffers (yuv_planes == nullptr).
        if (!yuv_planes && src_y != buffer_y_) {
            // We need to copy from src_y/u/v to buffer_y_/u_/v
            // because obs_jpegxs_source uses get_y_buffer() which points to buffer_y_
            
            size_t pixel_size = (bit_depth_ > 8) ? 2 : 1;
            size_t chroma_height = height_;
            if (format_ == 2) chroma_height = height_ / 2; // 4:2:0
            
            uint32_t dst_stride_y = width_ * pixel_size;
            uint32_t dst_stride_uv = dst_stride_y;
            if (format_ == 2 || format_ == 3) dst_stride_uv = dst_stride_y / 2;
            
            uint32_t src_stride_y = output_frame.image.stride[0];
            uint32_t src_stride_uv = output_frame.image.stride[1]; // Assuming same for u/v
            
            // SVT-JPEG-XS decoder returns stride in elements for > 8 bit.
            // We need bytes for memcpy pointer arithmetic.
            if (bit_depth_ > 8) {
                src_stride_y *= 2;
                src_stride_uv *= 2;
            }
            
            // Copy Y
            for (uint32_t y = 0; y < height_; ++y) {
                memcpy(buffer_y_ + y * dst_stride_y, src_y + y * src_stride_y, dst_stride_y);
            }
            
            // Copy U
            for (uint32_t y = 0; y < chroma_height; ++y) {
                 memcpy(buffer_u_ + y * dst_stride_uv, src_u + y * src_stride_uv, dst_stride_uv);
            }
            
            // Copy V
            for (uint32_t y = 0; y < chroma_height; ++y) {
                 memcpy(buffer_v_ + y * dst_stride_uv, src_v + y * src_stride_uv, dst_stride_uv);
            }
            
            // Note: we don't need to update src_y/u/v pointers here because we are done with them
            // for this branch (yuv_planes is null).
        }

        // Copy to caller's buffers IF provided
        if (yuv_planes && linesize) {
            size_t pixel_size = (bit_depth_ > 8) ? 2 : 1;
            size_t chroma_height = height_;
            if (format_ == 2) chroma_height = height_ / 2; // 4:2:0
            
            uint32_t stride_y = width_ * pixel_size;
            uint32_t stride_uv = stride_y;
            if (format_ == 2 || format_ == 3) stride_uv = stride_y / 2;

            // Y Plane
            if (yuv_planes[0] && src_y) {
                for (uint32_t y = 0; y < height_; ++y) {
                    memcpy(yuv_planes[0] + y * linesize[0], src_y + y * stride_y, 
                           (linesize[0] < stride_y) ? linesize[0] : stride_y);
                }
            }
            
            // U Plane
            if (yuv_planes[1] && src_u) {
                for (uint32_t y = 0; y < chroma_height; ++y) {
                    memcpy(yuv_planes[1] + y * linesize[1], src_u + y * stride_uv, 
                           (linesize[1] < stride_uv) ? linesize[1] : stride_uv);
                }
            }
            
            // V Plane
            if (yuv_planes[2] && src_v) {
                for (uint32_t y = 0; y < chroma_height; ++y) {
                    memcpy(yuv_planes[2] + y * linesize[2], src_v + y * stride_uv, 
                           (linesize[2] < stride_uv) ? linesize[2] : stride_uv);
                }
            }
        }

        stats_.frames_decoded++;
        stats_.bytes_decoded += input_size;
        return true;
    } else if (ret == SvtJxsErrorDecoderConfigChange) {
         blog(LOG_WARNING, "[JpegXSDecoder] Config change detected in get_frame, attempting reinit");
         dec_api->private_ptr = nullptr;
         first_frame_ = true;
    } else {
         blog(LOG_ERROR, "[JpegXSDecoder] get_frame failed with error 0x%x", ret);
    }
    
    return false;
}

} // namespace jpegxs
