/*
 * JPEG XS Decoder Wrapper Implementation
 */

#include "jpegxs_decoder.h"
#include <cstring>
#include <obs-module.h>

// SVT-JPEG-XS decoder API
#include <svt-jpegxs/SvtJpegxsDec.h>

namespace jpegxs {

JpegXSDecoder::JpegXSDecoder()
    : decoder_handle_(nullptr)
    , width_(0)
    , height_(0)
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

bool JpegXSDecoder::decode_frame(const std::vector<uint8_t> &input,
                                 uint8_t *yuv_planes[3], uint32_t linesize[3])
{
    if (!decoder_handle_) {
        return false;
    }
    
    svt_jpeg_xs_decoder_api_t *dec_api = static_cast<svt_jpeg_xs_decoder_api_t*>(decoder_handle_);
    
    // Check if decoder is initialized (has parsed first frame)
    if (first_frame_ && dec_api->private_ptr == nullptr) {
        // DEBUG: Log first 8 bytes of received bitstream
        if (input.size() >= 8) {
            const uint8_t* b = input.data();
            blog(LOG_INFO, "[JpegXSDecoder] Init Frame Bytes: %02X %02X %02X %02X %02X %02X %02X %02X (Size: %zu)", 
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], input.size());
        } else {
            blog(LOG_ERROR, "[JpegXSDecoder] Received bitstream too small: %zu bytes", input.size());
        }

        // Initialize decoder with first frame bitstream
        svt_jpeg_xs_image_config_t image_config;
        SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_init(
            SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
            dec_api, input.data(), input.size(), &image_config);
        
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
        first_frame_ = false;
        
        blog(LOG_INFO, "[JpegXSDecoder] Initialized: %ux%u, %u bits, Format: %d", 
             width_, height_, image_config.bit_depth, image_config.format);
        
        // Resize internal persistent buffers
        // We must ensure we allocate enough for the stride/format
        // If format is 4:2:2 or 4:4:4, we need bigger buffers.
        // For now, assume 4:2:0 as per OBS requirement.
        
        buffer_y_.resize(width_ * height_);
        buffer_u_.resize(width_ * height_ / 2); // 4:2:0 assumption
        buffer_v_.resize(width_ * height_ / 2);
    }
    
    // Prepare input frame
    svt_jpeg_xs_frame_t input_frame;
    memset(&input_frame, 0, sizeof(input_frame));
    
    input_frame.bitstream.buffer = const_cast<uint8_t*>(input.data());
    input_frame.bitstream.allocation_size = input.size();
    input_frame.bitstream.used_size = input.size();
    
    // Use internal persistent buffers for the decoder to write into.
    // This is critical because the decoder is threaded and may write to these buffers
    // asynchronously or hold references. If we use stack/temporary buffers, we risk use-after-free.
    input_frame.image.data_yuv[0] = buffer_y_.data();
    input_frame.image.data_yuv[1] = buffer_u_.data();
    input_frame.image.data_yuv[2] = buffer_v_.data();
    
    input_frame.image.stride[0] = linesize[0];
    input_frame.image.stride[1] = linesize[1];
    input_frame.image.stride[2] = linesize[2];
    input_frame.image.alloc_size[0] = buffer_y_.size();
    input_frame.image.alloc_size[1] = buffer_u_.size();
    input_frame.image.alloc_size[2] = buffer_v_.size();
    
    // Send frame to decoder (frame-based mode)
    // blocking_flag = 1 ensures we wait for the frame to be accepted/processed
    SvtJxsErrorType_t ret = svt_jpeg_xs_decoder_send_frame(dec_api, &input_frame, 1);
    
    if (ret != SvtJxsErrorNone) {
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
        if (!src_y) src_y = buffer_y_.data();
        if (!src_u) src_u = buffer_u_.data();
        if (!src_v) src_v = buffer_v_.data();
        
        // Copy to caller's buffers
        if (yuv_planes[0] && src_y) memcpy(yuv_planes[0], src_y, linesize[0] * height_);
        if (yuv_planes[1] && src_u) memcpy(yuv_planes[1], src_u, linesize[1] * height_ / 2);
        if (yuv_planes[2] && src_v) memcpy(yuv_planes[2], src_v, linesize[2] * height_ / 2);

        stats_.frames_decoded++;
        stats_.bytes_decoded += input.size();
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
