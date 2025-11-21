/*
 * JPEG XS Encoder Wrapper Implementation
 */

#include "jpegxs_encoder.h"
#include <cstring>
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
}

bool JpegXSEncoder::initialize(uint32_t width, uint32_t height,
                               uint32_t fps_num, uint32_t fps_den,
                               float bitrate_mbps, uint32_t threads_num)
{
    width_ = width;
    height_ = height;
    fps_num_ = fps_num;
    fps_den_ = fps_den;
    bitrate_mbps_ = bitrate_mbps;
    threads_num_ = threads_num;
    
    // Initialize SVT-JPEG-XS encoder
    svt_jpeg_xs_encoder_api_t *enc_api = new svt_jpeg_xs_encoder_api_t;
    memset(enc_api, 0, sizeof(*enc_api));
    
    // Load default parameters
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, enc_api);
    
    if (ret != SvtJxsErrorNone) {
        delete enc_api;
        return false;
    }
    
    // Set video format
    enc_api->source_width = width;
    enc_api->source_height = height;
    enc_api->input_bit_depth = 8;  // 8-bit for now (OBS default)
    enc_api->colour_format = COLOUR_FORMAT_PLANAR_YUV420;  // YUV 4:2:0
    
    // Calculate bits per pixel from bitrate
    float fps = (float)fps_num / fps_den;
    float total_pixels = width * height * fps;
    float bpp = (bitrate_mbps * 1e6f) / total_pixels;
    
    enc_api->bpp_numerator = (uint32_t)(bpp * 100.0f);  // Store as fixed point
    enc_api->bpp_denominator = 100;
    
    // Set low-latency parameters
    enc_api->cpu_profile = 0;  // 0 = Low latency
    enc_api->ndecomp_v = 2;    // Vertical decomposition levels
    enc_api->ndecomp_h = 5;    // Horizontal decomposition levels
    enc_api->threads_num = (threads_num > 0) ? threads_num : 4;   // Number of threads
    enc_api->use_cpu_flags = CPU_FLAGS_ALL;
    
    // Use codestream/frame packetization mode (0) to match decoder expectations
    // Mode 0 = One "packet" per frame (or few large chunks) - valid full Codestream
    enc_api->slice_packetization_mode = 0;
    enc_api->slice_height = 16;  // Still set slice height as it affects encoding structure internally
    
    // Initialize encoder instance
    ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, enc_api);
    
    if (ret != SvtJxsErrorNone) {
        delete enc_api;
        return false;
    }
    
    encoder_handle_ = enc_api;
    
    // Pre-allocate bitstream buffer (estimate 2x uncompressed size to be safe)
    // RGB equivalent size is usually enough
    size_t buffer_size = width_ * height_ * 3;
    bitstream_buffer_.resize(buffer_size);
    
    return true;
}

bool JpegXSEncoder::encode_frame(uint8_t *yuv_planes[3], uint32_t linesize[3],
                                 uint64_t timestamp,
                                 std::vector<uint8_t> &output)
{
    if (!encoder_handle_) {
        return false;
    }
    
    svt_jpeg_xs_encoder_api_t *enc_api = static_cast<svt_jpeg_xs_encoder_api_t*>(encoder_handle_);
    
    // Prepare input frame
    svt_jpeg_xs_frame_t input_frame;
    memset(&input_frame, 0, sizeof(input_frame));
    
    input_frame.image.data_yuv[0] = yuv_planes[0];
    input_frame.image.data_yuv[1] = yuv_planes[1];
    input_frame.image.data_yuv[2] = yuv_planes[2];
    input_frame.image.stride[0] = linesize[0];
    input_frame.image.stride[1] = linesize[1];
    input_frame.image.stride[2] = linesize[2];
    
    // Set alloc_size (required by SVT-JPEG-XS)
    // Note: SVT-JPEG-XS expects alloc_size to be the full buffer size for the plane
    // It seems to validate this against stride * height
    input_frame.image.alloc_size[0] = linesize[0] * height_;
    input_frame.image.alloc_size[1] = linesize[1] * height_ / 2; // 4:2:0
    input_frame.image.alloc_size[2] = linesize[2] * height_ / 2; // 4:2:0
    
    // IMPORTANT: SVT-JPEG-XS requires user_prv_ctx_ptr to be NULL if not used
    input_frame.user_prv_ctx_ptr = nullptr;
    
    // Use pre-allocated buffer
    // Ensure buffer is large enough (resize if dimensions changed significantly, though unlikely)
    size_t required_size = width_ * height_ * 3;
    if (bitstream_buffer_.size() < required_size) {
        bitstream_buffer_.resize(required_size);
    }
    
    input_frame.bitstream.buffer = bitstream_buffer_.data();
    input_frame.bitstream.allocation_size = bitstream_buffer_.size();
    input_frame.bitstream.used_size = 0;
    
    // Send frame to encoder (non-blocking)
    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_send_picture(enc_api, &input_frame, 1);
    
    if (ret != SvtJxsErrorNone && ret != SvtJxsErrorNoErrorEmptyQueue) {
        fprintf(stderr, "[JpegXSEncoder] send_picture failed with error %d\n", ret);
        return false;
    }
    
    // Get encoded packet(s) - loop until frame is finished
    bool finished_frame = false;
    int packet_count = 0;
    
    while (!finished_frame) {
        svt_jpeg_xs_frame_t output_frame;
        memset(&output_frame, 0, sizeof(output_frame));
        
        // Blocking wait for packet
        ret = svt_jpeg_xs_encoder_get_packet(enc_api, &output_frame, 1);
        
        if (ret == SvtJxsErrorNone) {
            if (output_frame.bitstream.used_size > 0) {
                // Sanity check on size
                if (output_frame.bitstream.used_size > bitstream_buffer_.size()) {
                    fprintf(stderr, "[JpegXSEncoder] Invalid packet size: %u\n", output_frame.bitstream.used_size);
                    return false;
                }
                
        output.insert(output.end(), 
                     output_frame.bitstream.buffer, 
                     output_frame.bitstream.buffer + output_frame.bitstream.used_size);
        
        // DEBUG: Log first 8 bytes of the first packet
        if (stats_.frames_encoded < 5 && output_frame.bitstream.used_size >= 8) {
            uint8_t* b = output_frame.bitstream.buffer;
            blog(LOG_INFO, "[JpegXSEncoder] Frame %llu, Packet Bytes: %02X %02X %02X %02X %02X %02X %02X %02X", 
                stats_.frames_encoded + 1, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        }
        
        stats_.bytes_encoded += output_frame.bitstream.used_size;
            }
            
            // Check if this is the last packet of the frame
            if (output_frame.bitstream.last_packet_in_frame) {
                finished_frame = true;
            }
            
            packet_count++;
            if (packet_count > 1000) {
                fprintf(stderr, "[JpegXSEncoder] Too many packets for one frame (>1000)\n");
                break;
            }
        } else if (ret == SvtJxsErrorNoErrorEmptyQueue) {
            // If we get empty queue but haven't seen EOF, it might be an issue with blocking mode
            // or the encoder is buffering. But with blocking_flag=1, it should wait.
            // If it returns EmptyQueue, maybe we are done?
            if (packet_count > 0) {
                finished_frame = true;
            } else {
                fprintf(stderr, "[JpegXSEncoder] Empty queue returned without any packets\n");
                return false;
            }
        } else {
            fprintf(stderr, "[JpegXSEncoder] get_packet failed with error %d\n", ret);
            return false;
        }
    }
    
    if (packet_count > 0) {
        stats_.frames_encoded++;
        return true;
    }
    
    return false;
}

bool JpegXSEncoder::flush(std::vector<uint8_t> &output)
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
        output.insert(output.end(), 
                     output_frame.bitstream.buffer, 
                     output_frame.bitstream.buffer + output_frame.bitstream.used_size);
        
        if (output_frame.bitstream.buffer) {
            delete[] output_frame.bitstream.buffer;
        }
        return true;
    }
    
    return false;
}
