#include <svt-jpegxs/SvtJpegxsEnc.h>
#include <iostream>
#include <vector>
#include <cstring>

#ifdef _WIN32
#include <malloc.h>
#define ALIGNED_ALLOC(size, align) _aligned_malloc(size, align)
#define ALIGNED_FREE(ptr) _aligned_free(ptr)
#else
#include <stdlib.h>
#define ALIGNED_ALLOC(size, align) aligned_alloc(align, size)
#define ALIGNED_FREE(ptr) free(ptr)
#endif

void test_10bit_encoding_elements_stride() {
    std::cout << "Testing SVT-JPEG-XS 10-bit encoding (4:2:2) - Stride in ELEMENTS?..." << std::endl;

    svt_jpeg_xs_encoder_api_t enc_api;
    memset(&enc_api, 0, sizeof(enc_api));

    SvtJxsErrorType_t ret = svt_jpeg_xs_encoder_load_default_parameters(
        SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc_api);

    if (ret != SvtJxsErrorNone) {
        std::cerr << "Failed to load defaults: " << ret << std::endl;
        return;
    }

    uint32_t width = 1920;
    uint32_t height = 1080;
    enc_api.source_width = width;
    enc_api.source_height = height;
    enc_api.input_bit_depth = 10;
    enc_api.colour_format = COLOUR_FORMAT_PLANAR_YUV422;
    enc_api.bpp_numerator = 160; 
    enc_api.bpp_denominator = 100;
    enc_api.threads_num = 1;

    std::cout << "Initializing encoder..." << std::endl;
    ret = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc_api);
    if (ret != SvtJxsErrorNone) {
        std::cerr << "Encoder init failed: " << ret << std::endl;
        return;
    }
    std::cout << "Encoder init success." << std::endl;

    svt_jpeg_xs_frame_t input_frame;
    memset(&input_frame, 0, sizeof(input_frame));

    // TRY STRIDE = WIDTH (Elements/Shorts count)
    uint32_t stride_y = width; 
    uint32_t stride_uv = width / 2; // 4:2:2 width halved
    
    // Actual allocation MUST be in bytes (width * 2 bytes)
    uint32_t alloc_y = width * 2 * height;
    uint32_t alloc_uv = (width / 2) * 2 * height;

    std::cout << "Passing Stride: Y=" << stride_y << " U=" << stride_uv << std::endl;
    std::cout << "Alloc sizes: Y=" << alloc_y << " U=" << alloc_uv << std::endl;

    void* buf_y = ALIGNED_ALLOC(alloc_y, 64);
    void* buf_u = ALIGNED_ALLOC(alloc_uv, 64);
    void* buf_v = ALIGNED_ALLOC(alloc_uv, 64);
    memset(buf_y, 0x80, alloc_y);
    memset(buf_u, 0x80, alloc_uv);
    memset(buf_v, 0x80, alloc_uv);

    input_frame.image.data_yuv[0] = buf_y;
    input_frame.image.data_yuv[1] = buf_u;
    input_frame.image.data_yuv[2] = buf_v;
    input_frame.image.stride[0] = stride_y;
    input_frame.image.stride[1] = stride_uv;
    input_frame.image.stride[2] = stride_uv;
    input_frame.image.alloc_size[0] = alloc_y;
    input_frame.image.alloc_size[1] = alloc_uv;
    input_frame.image.alloc_size[2] = alloc_uv;

    size_t bitstream_size = width * height * 4; 
    void* bitstream = ALIGNED_ALLOC(bitstream_size, 64);
    input_frame.bitstream.buffer = (uint8_t*)bitstream;
    input_frame.bitstream.allocation_size = bitstream_size;
    input_frame.bitstream.used_size = 0;

    std::cout << "Sending picture..." << std::endl;
    ret = svt_jpeg_xs_encoder_send_picture(&enc_api, &input_frame, 1);
    
    if (ret != SvtJxsErrorNone) {
        std::cerr << "Send picture failed: " << ret << " (0x" << std::hex << ret << ")" << std::endl;
    } else {
        std::cout << "Send picture success!" << std::endl;
    }

    ALIGNED_FREE(buf_y);
    ALIGNED_FREE(buf_u);
    ALIGNED_FREE(buf_v);
    ALIGNED_FREE(bitstream);
    svt_jpeg_xs_encoder_close(&enc_api);
}

int main() {
    test_10bit_encoding_elements_stride();
    return 0;
}
