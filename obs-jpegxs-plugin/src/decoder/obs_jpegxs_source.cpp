/*
 * OBS JPEG XS Source Implementation
 * Handles OBS source callbacks and coordinates decoding/receiving
 */

#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <chrono>
#include <algorithm>

#include "obs_jpegxs_source.h"
#include "jpegxs_decoder.h"
#include "../network/rtp_packet.h"
#include "../network/srt_transport.h"
#include "../network/udp_socket.h"

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

using jpegxs::RTPDepacketizer;
using jpegxs::SRTTransport;
using jpegxs::JpegXSDecoder;
using jpegxs::UDPSocket;

enum TransportMode {
    MODE_SRT = 0,
    MODE_ST2110 = 1
};

struct jpegxs_source {
    obs_source_t *source;
    
    // JPEG XS decoder
    std::unique_ptr<JpegXSDecoder> decoder;
    
    // Network transport
    TransportMode mode;
    std::unique_ptr<RTPDepacketizer> rtp_depacketizer;
    
    // SRT
    std::unique_ptr<SRTTransport> srt_transport;
    
    // UDP / ST 2110
    std::unique_ptr<UDPSocket> udp_socket;
    std::unique_ptr<UDPSocket> audio_udp_socket;
    
    // Configuration
    uint32_t width;
    uint32_t height;
    
    // SRT Config
    std::string srt_url;
    std::string srt_passphrase;
    uint32_t srt_latency_ms;
    uint16_t parsed_srt_port = 9000;
    std::string parsed_srt_host = "0.0.0.0";
    
    // ST 2110 Config
    std::string st2110_multicast_ip; // Optional, if empty use unicast bind
    uint16_t st2110_port;
    uint16_t st2110_audio_port;
    std::string st2110_interface_ip;
    
    uint32_t threads_num;
    
    // Receive thread
    std::thread receive_thread;
    std::thread audio_thread;
    std::atomic<bool> active;
    
    // Statistics
    uint64_t total_frames;
    uint64_t dropped_frames;
};

// Forward declarations
static const char *jpegxs_source_getname(void *unused);
static void *jpegxs_source_create(obs_data_t *settings, obs_source_t *source);
static void jpegxs_source_destroy(void *data);
static void jpegxs_source_update(void *data, obs_data_t *settings);
static void jpegxs_source_show(void *data);
static void jpegxs_source_hide(void *data);
static uint32_t jpegxs_source_get_width(void *data);
static uint32_t jpegxs_source_get_height(void *data);
static obs_properties_t *jpegxs_source_properties(void *unused);
static void jpegxs_source_get_defaults(obs_data_t *settings);

static void process_frame_data(jpegxs_source *context, const uint8_t* bitstream, size_t bitstream_size, uint32_t rtp_timestamp)
{
    if (!context->decoder) return;

    static uint64_t last_log_time = 0;
    static uint64_t accumulated_decode_time_ns = 0;
    static uint64_t frame_count_log = 0;
    
    uint64_t start_decode = os_gettime_ns();

    // Decode using internal buffers
    if (context->decoder->decode_frame(bitstream, bitstream_size, nullptr, nullptr)) {
        
        uint64_t end_decode = os_gettime_ns();
        accumulated_decode_time_ns += (end_decode - start_decode);
        frame_count_log++;
        
        uint64_t current_time = os_gettime_ns();
        if (current_time - last_log_time >= 1000000000ULL) {
            double avg_decode = (double)accumulated_decode_time_ns / frame_count_log / 1000000.0;
            blog(LOG_INFO, "[JPEG XS Source] Stats (1s): Frames=%llu, Avg Decode=%.2fms, Dropped=%llu",
                 frame_count_log, avg_decode, context->dropped_frames);
            
            last_log_time = current_time;
            accumulated_decode_time_ns = 0;
            frame_count_log = 0;
        }

        struct obs_source_frame frame;
        memset(&frame, 0, sizeof(frame));
        
        uint32_t width = context->decoder->getWidth();
        uint32_t height = context->decoder->getHeight();
        int bit_depth = context->decoder->getBitDepth();
        int dec_format = context->decoder->getFormat(); 
        
        if (width != context->width || height != context->height) {
            context->width = width;
            context->height = height;
        }
        
        enum video_format obs_fmt = VIDEO_FORMAT_NONE;
        if (bit_depth == 8) {
            if (dec_format == 2) obs_fmt = VIDEO_FORMAT_I420;
            else if (dec_format == 3) obs_fmt = VIDEO_FORMAT_I422;
            else if (dec_format == 4) obs_fmt = VIDEO_FORMAT_I444;
        } else if (bit_depth == 10) {
            if (dec_format == 2) obs_fmt = VIDEO_FORMAT_I010;
            else if (dec_format == 3) obs_fmt = VIDEO_FORMAT_I210;
            else if (dec_format == 4) obs_fmt = VIDEO_FORMAT_I412; // Use I412 for >8-bit 4:4:4 (16-bit container)
        }
        
        if (obs_fmt == VIDEO_FORMAT_NONE) return;
        
        frame.format = obs_fmt;
        frame.width = width;
        frame.height = height;
        frame.data[0] = (uint8_t*)context->decoder->get_y_buffer();
        frame.data[1] = (uint8_t*)context->decoder->get_u_buffer();
        frame.data[2] = (uint8_t*)context->decoder->get_v_buffer();
        
        uint32_t bpp = (bit_depth > 8) ? 2 : 1;
        frame.linesize[0] = width * bpp;
        
        if (dec_format == 2 || dec_format == 3) {
            frame.linesize[1] = (width / 2) * bpp;
            frame.linesize[2] = (width / 2) * bpp;
        } else {
            frame.linesize[1] = width * bpp;
            frame.linesize[2] = width * bpp;
        }
        
        // Timestamp handling: Convert RTP (90kHz) to NS
        // We need to handle wrapping and offset relative to system time
        // For now, we use a simple relative offset from the first frame
        // But to fix "rubber banding", we should trust the RTP intervals.
        // RTP 90kHz = 90000 ticks per second.
        // 1 tick = 1/90000 sec = 11111.11 ns.
        
        /*
        static uint64_t first_sys_time = 0;
        static uint32_t first_rtp_time = 0;
        static bool first_ts_set = false;
        
        if (!first_ts_set) {
            first_sys_time = os_gettime_ns();
            first_rtp_time = rtp_timestamp;
            first_ts_set = true;
        }
        
        // Handle wrap-around logic simply for now (assuming no huge gaps)
        int64_t rtp_diff = (int64_t)rtp_timestamp - first_rtp_time;
        // if (rtp_diff < -2000000000) rtp_diff += 4294967296; // Handle wrap
        
        uint64_t pts_ns = first_sys_time + (rtp_diff * 1000000000ULL / 90000ULL);
        */
        
        // LOW LATENCY OPTIMIZATION:
        // Ignore RTP timestamp for display sync. Use Time of Arrival.
        // This eliminates drift-induced buffering in OBS and guarantees "freshest" frame display.
        // Since we are unbuffered, this is the correct behavior for <20ms latency.
        frame.timestamp = os_gettime_ns();
        
        // OBS has a built-in smoothing buffer for async video sources.
        // For true low latency, we need to bypass this as much as possible.
        // Timestamp must be strictly monotonic and close to system time.
        
        frame.full_range = false; // Partial
        frame.flip = false;
        
        // Use OBS helper to get correct matrix/range for format/space
        // Assuming Rec.709 for HD content
        const struct video_output_info *voi = video_output_get_info(obs_get_video());
        bool full_range = false; // Limited range is standard for broadcast
        enum video_colorspace cs = VIDEO_CS_709;
        
        float matrix[16];
        float range_min[3];
        float range_max[3];
        
        video_format_get_parameters(cs, VIDEO_RANGE_PARTIAL, matrix, range_min, range_max);
        
        memcpy(frame.color_matrix, matrix, sizeof(matrix));
        memcpy(frame.color_range_min, range_min, sizeof(range_min));
        memcpy(frame.color_range_max, range_max, sizeof(range_max));
        
        
        // Drain logic:
        // If we are buffering too many frames (latency), skip older ones.
        // But RTPDepacketizer already gives us the latest complete frame if we poll fast enough.
        // The bottleneck might be in OBS processing the frames we push.
        // Since we use 'os_gettime_ns()', OBS will try to display immediately.
        
        // DRAIN: Check if we have processed a frame too recently to catch up?
        // No, we want to output as fast as possible.
        
        obs_source_output_video(context->source, &frame);
        context->total_frames++;
        
    } else {
        context->dropped_frames++;
    }
}

static void process_audio_packet(jpegxs_source *context, const uint8_t* data, size_t size, uint32_t timestamp)
{
    // Expecting L16 Stereo (4 bytes per sample frame)
    // RTP Header (12 bytes) is already stripped or handled by caller?
    // Actually this function receives raw packet or payload?
    // Let's assume payload for now, or check header.
    
    // Wait, receive_audio_loop_udp reads raw packet. 
    // We need to parse RTP header to find payload offset.
    
    if (size < 12) return;
    
    // Basic RTP Header Check
    uint8_t v = (data[0] >> 6) & 0x03;
    if (v != 2) return;
    
    uint8_t cc = data[0] & 0x0F;
    size_t header_len = 12 + (cc * 4);
    
    if (size <= header_len) return;
    
    const uint8_t *payload = data + header_len;
    size_t payload_len = size - header_len;
    
    // Assuming L16 Stereo (4 bytes per frame: 2 bytes left, 2 bytes right)
    uint32_t frames = payload_len / 4;
    if (frames == 0) return;
    
    struct obs_source_audio audio;
    memset(&audio, 0, sizeof(audio));
    
    audio.speakers = SPEAKERS_STEREO;
    audio.samples_per_sec = 48000;
    audio.format = AUDIO_FORMAT_FLOAT_PLANAR; // OBS internal format
    audio.frames = frames;
    audio.timestamp = os_gettime_ns(); // Low latency: use arrival time
    
    // Allocate buffers for planar float
    // obs_source_audio_init doesn't alloc data pointers usually, we need to provide them.
    // But obs_source_output_audio copies data.
    // We need separate vectors for L and R.
    
    std::vector<float> ch_left(frames);
    std::vector<float> ch_right(frames);
    
    const int16_t *samples = (const int16_t*)payload;
    
    for (uint32_t i = 0; i < frames; i++) {
        // Read Big Endian Int16
        // Payload is byte stream.
        uint8_t h1 = payload[i*4 + 0];
        uint8_t l1 = payload[i*4 + 1];
        int16_t s1 = (int16_t)((h1 << 8) | l1);
        
        uint8_t h2 = payload[i*4 + 2];
        uint8_t l2 = payload[i*4 + 3];
        int16_t s2 = (int16_t)((h2 << 8) | l2);
        
        // Convert to Float [-1.0, 1.0]
        ch_left[i] = s1 / 32768.0f;
        ch_right[i] = s2 / 32768.0f;
    }
    
    const uint8_t *planes[2];
    planes[0] = (const uint8_t*)ch_left.data();
    planes[1] = (const uint8_t*)ch_right.data();
    
    audio.data[0] = (const uint8_t*)ch_left.data();
    audio.data[1] = (const uint8_t*)ch_right.data();
    
    obs_source_output_audio(context->source, &audio);
}

static void receive_loop_audio(jpegxs_source *context)
{
    blog(LOG_INFO, "[JPEG XS] Audio Receive thread started");
    
    std::vector<uint8_t> buffer(2048);
    
    while (context->active) {
        std::string src_ip;
        uint16_t src_port;
        
        if (!context->audio_udp_socket) break;
        
        int received = context->audio_udp_socket->recvFrom(buffer.data(), buffer.size(), src_ip, src_port);
        
        if (received > 0) {
             process_audio_packet(context, buffer.data(), received, 0);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
     blog(LOG_INFO, "[JPEG XS] Audio Receive thread stopped");
}

static void receive_loop_udp(jpegxs_source *context)
{
    blog(LOG_INFO, "[JPEG XS] UDP Receive thread started");
    
    std::vector<uint8_t> buffer(2048); // RTP packets usually < 1500
    
    while (context->active) {
        std::string src_ip;
        uint16_t src_port;
        
        int received = context->udp_socket->recvFrom(buffer.data(), buffer.size(), src_ip, src_port);
        
        if (received > 0) {
            if (context->rtp_depacketizer->processPacket(buffer.data(), received)) {
                if (context->rtp_depacketizer->isFrameReady()) {
                    size_t frame_size = 0;
                    const uint8_t* frame_data = context->rtp_depacketizer->getFrameData(frame_size);
                    // Pass RTP timestamp
                    process_frame_data(context, frame_data, frame_size, context->rtp_depacketizer->getCurrentTimestamp());
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Reduced sleep for lower latency
        }
    }
    
    blog(LOG_INFO, "[JPEG XS] UDP Receive thread stopped");
}

static void receive_loop_srt(jpegxs_source *context)
{
    blog(LOG_INFO, "[JPEG XS] SRT Receive thread started");
    
    context->srt_transport->setDataCallback([context](const uint8_t *data, size_t size) {
        if (!context->active) return;
        if (context->rtp_depacketizer->processPacket(data, size)) {
            if (context->rtp_depacketizer->isFrameReady()) {
                size_t frame_size = 0;
                const uint8_t* frame_data = context->rtp_depacketizer->getFrameData(frame_size);
                // Pass RTP timestamp
                process_frame_data(context, frame_data, frame_size, context->rtp_depacketizer->getCurrentTimestamp());
            }
        }
    });
    
    while (context->active) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    if (context->srt_transport) {
        context->srt_transport->setDataCallback(nullptr);
    }
    
    blog(LOG_INFO, "[JPEG XS] SRT Receive thread stopped");
}

static bool transport_mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
    const char *mode_str = obs_data_get_string(settings, "transport_mode");
    bool is_st2110 = (strcmp(mode_str, "ST 2110-22 (UDP/Multicast)") == 0);
    
    obs_property_t *group_srt = obs_properties_get(props, "group_srt");
    obs_property_t *group_st2110 = obs_properties_get(props, "group_st2110");
    
    obs_property_set_visible(group_srt, !is_st2110);
    obs_property_set_visible(group_st2110, is_st2110);
    
    return true;
}

void register_jpegxs_source(struct obs_source_info *info)
{
    info->id = "jpegxs_source";
    info->type = OBS_SOURCE_TYPE_INPUT;
    info->output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
    info->get_name = jpegxs_source_getname;
    info->create = jpegxs_source_create;
    info->destroy = jpegxs_source_destroy;
    info->update = jpegxs_source_update;
    info->show = jpegxs_source_show;
    info->hide = jpegxs_source_hide;
    info->get_width = jpegxs_source_get_width;
    info->get_height = jpegxs_source_get_height;
    info->get_properties = jpegxs_source_properties;
    info->get_defaults = jpegxs_source_get_defaults;
}

static const char *jpegxs_source_getname(void *unused)
{
    UNUSED_PARAMETER(unused);
    return "JPEG XS Source (RTP/SRT/ST2110)";
}

static void *jpegxs_source_create(obs_data_t *settings, obs_source_t *source)
{
    blog(LOG_INFO, "[JPEG XS] Creating source instance");
    
    // Enable low-latency mode (disable OBS buffering)
    obs_source_set_async_unbuffered(source, true);

    jpegxs_source *context = new jpegxs_source();
    context->source = source;
    context->active = false;
    context->total_frames = 0;
    context->dropped_frames = 0;
    context->width = 1920;
    context->height = 1080;
    
    jpegxs_source_update(context, settings);
    
    return context;
}

static void jpegxs_source_destroy(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    blog(LOG_INFO, "[JPEG XS] Destroying source instance");
    
    if (context->active) {
        jpegxs_source_hide(context);
    }
    
    delete context;
}

#include <fstream>
#include <sstream>

// ... existing includes ...

// Simple SDP parser helper
struct SDPInfo {
    std::string dest_ip;
    uint16_t port = 0;
    uint16_t audio_port = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps_num = 0;
    uint32_t fps_den = 0;
};

static SDPInfo parse_sdp_file(const std::string& path) {
    SDPInfo info;
    std::ifstream file(path);
    if (!file.is_open()) return info;

    std::string line;
    bool in_audio = false;
    
    while (std::getline(file, line)) {
        if (line.rfind("c=IN IP4 ", 0) == 0) {
            if (!in_audio) {
                info.dest_ip = line.substr(9);
                // Trim whitespace
                info.dest_ip.erase(info.dest_ip.find_last_not_of(" \n\r\t") + 1);
            }
        } else if (line.rfind("m=video ", 0) == 0) {
            std::stringstream ss(line.substr(8));
            ss >> info.port;
            in_audio = false;
        } else if (line.rfind("m=audio ", 0) == 0) {
            std::stringstream ss(line.substr(8));
            ss >> info.audio_port;
            in_audio = true;
        } else if (line.rfind("a=fmtp:", 0) == 0) {
            // Parse fmtp line for width/height/framerate
            if (line.find("width=") != std::string::npos) {
                size_t pos = line.find("width=");
                info.width = std::stoi(line.substr(pos + 6));
            }
            if (line.find("height=") != std::string::npos) {
                size_t pos = line.find("height=");
                info.height = std::stoi(line.substr(pos + 7));
            }
            if (line.find("exactframerate=") != std::string::npos) {
                size_t pos = line.find("exactframerate=");
                std::string fps_str = line.substr(pos + 15);
                size_t slash = fps_str.find('/');
                if (slash != std::string::npos) {
                    try {
                        info.fps_num = std::stoi(fps_str.substr(0, slash));
                        // Find end of number (either ; or end of string)
                        size_t end = fps_str.find(';', slash + 1);
                        if (end == std::string::npos) end = fps_str.length();
                        info.fps_den = std::stoi(fps_str.substr(slash + 1, end - slash - 1));
                    } catch (...) {}
                }
            }
        }
    }
    return info;
}

static void jpegxs_source_update(void *data, obs_data_t *settings)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    const char *mode_str = obs_data_get_string(settings, "transport_mode");
    if (strcmp(mode_str, "ST 2110-22 (UDP/Multicast)") == 0) {
        context->mode = MODE_ST2110;
    } else {
        context->mode = MODE_SRT;
    }
    
    // SRT
    context->srt_url = obs_data_get_string(settings, "srt_url");
    context->srt_latency_ms = (uint32_t)obs_data_get_int(settings, "srt_latency");
    context->srt_passphrase = obs_data_get_string(settings, "srt_passphrase");
    
    // ST 2110 / UDP Configuration logic
    std::string sdp_path = obs_data_get_string(settings, "sdp_file_path");
    if (!sdp_path.empty()) {
        SDPInfo sdp = parse_sdp_file(sdp_path);
        if (sdp.port > 0) {
            context->st2110_port = sdp.port;
            context->st2110_multicast_ip = sdp.dest_ip;
            if (sdp.audio_port > 0) context->st2110_audio_port = sdp.audio_port;
            else context->st2110_audio_port = sdp.port + 2; // Default logic
            
            // Also update width/height if found?
            if (sdp.width > 0 && sdp.height > 0) {
                context->width = sdp.width;
                context->height = sdp.height;
            }
            blog(LOG_INFO, "[JPEG XS] Parsed SDP: IP=%s Video=%u Audio=%u %ux%u", 
                 sdp.dest_ip.c_str(), sdp.port, context->st2110_audio_port, sdp.width, sdp.height);
        }
    } else {
        // Manual override
        context->st2110_port = (uint16_t)obs_data_get_int(settings, "st2110_port");
        context->st2110_multicast_ip = obs_data_get_string(settings, "st2110_multicast_ip");
        context->st2110_audio_port = (uint16_t)obs_data_get_int(settings, "st2110_audio_port");
        if (context->st2110_audio_port == 0) context->st2110_audio_port = context->st2110_port + 2;
        
        // Optional Manual Format
        uint32_t manual_w = (uint32_t)obs_data_get_int(settings, "manual_width");
        uint32_t manual_h = (uint32_t)obs_data_get_int(settings, "manual_height");
        if (manual_w > 0 && manual_h > 0) {
            context->width = manual_w;
            context->height = manual_h;
        }
    }
    context->st2110_interface_ip = obs_data_get_string(settings, "st2110_interface_ip");
    
    context->threads_num = (uint32_t)obs_data_get_int(settings, "threads");
    
    // Parse SRT URL if needed (Legacy logic)
    const char* url = context->srt_url.c_str();
    if (url && strncmp(url, "srt://", 6) == 0) {
        std::string url_str(url);
        size_t port_pos = url_str.find_last_of(':');
        if (port_pos != std::string::npos && port_pos > 6) {
            try {
                std::string p = url_str.substr(port_pos + 1);
                size_t q = p.find('?');
                if (q != std::string::npos) p = p.substr(0, q);
                context->parsed_srt_port = std::stoi(p);
            } catch (...) {}
        }
    }
}

static void jpegxs_source_show(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    try {
        blog(LOG_INFO, "[JPEG XS] Starting source");
        
        // Auto-detect threads if 0
        uint32_t threads = context->threads_num;
        if (threads == 0) {
            unsigned int cores = std::thread::hardware_concurrency();
            threads = (cores > 0) ? cores : 8;
            blog(LOG_INFO, "[JPEG XS] Auto-detected %u threads for decoder", threads);
        }
        
        context->decoder = std::make_unique<JpegXSDecoder>();
        context->decoder->initialize(0, 0, threads);
        
        context->rtp_depacketizer = std::make_unique<RTPDepacketizer>();
        context->active = true;
        
        if (context->mode == MODE_SRT) {
            SRTTransport::Config srt_config;
            srt_config.mode = SRTTransport::Mode::LISTENER;
            srt_config.port = context->parsed_srt_port; // Or parse from URL
            if (srt_config.port == 0) srt_config.port = 9000;
            srt_config.address = "0.0.0.0";
            srt_config.latency_ms = context->srt_latency_ms;
            srt_config.passphrase = context->srt_passphrase;
            
            context->srt_transport = std::make_unique<SRTTransport>(srt_config);
            context->srt_transport->start();
            
            context->receive_thread = std::thread(receive_loop_srt, context);
        } else {
            // UDP Mode
            context->udp_socket = std::make_unique<UDPSocket>();
            if (context->udp_socket->bind(context->st2110_port, context->st2110_interface_ip.empty() ? "0.0.0.0" : context->st2110_interface_ip)) {
                blog(LOG_INFO, "[JPEG XS] Bound to UDP port %u", context->st2110_port);
                
                if (!context->st2110_multicast_ip.empty()) {
                    if (context->udp_socket->joinMulticast(context->st2110_multicast_ip, context->st2110_interface_ip.empty() ? "0.0.0.0" : context->st2110_interface_ip)) {
                        blog(LOG_INFO, "[JPEG XS] Joined multicast group %s", context->st2110_multicast_ip.c_str());
                    } else {
                        blog(LOG_ERROR, "[JPEG XS] Failed to join multicast group %s", context->st2110_multicast_ip.c_str());
                    }
                }
                
                context->udp_socket->setNonBlocking(true);
                context->receive_thread = std::thread(receive_loop_udp, context);
                
            } else {
                blog(LOG_ERROR, "[JPEG XS] Failed to bind UDP port %u", context->st2110_port);
            }
            
            // Audio UDP Socket
            if (context->st2110_audio_port > 0) {
                context->audio_udp_socket = std::make_unique<UDPSocket>();
                if (context->audio_udp_socket->bind(context->st2110_audio_port, context->st2110_interface_ip.empty() ? "0.0.0.0" : context->st2110_interface_ip)) {
                    blog(LOG_INFO, "[JPEG XS] Bound to Audio UDP port %u", context->st2110_audio_port);
                    
                    if (!context->st2110_multicast_ip.empty()) {
                        context->audio_udp_socket->joinMulticast(context->st2110_multicast_ip, context->st2110_interface_ip.empty() ? "0.0.0.0" : context->st2110_interface_ip);
                    }
                    
                    context->audio_udp_socket->setNonBlocking(true);
                    context->audio_thread = std::thread(receive_loop_audio, context);
                }
            }
        }
        
    } catch (...) {
        blog(LOG_ERROR, "[JPEG XS] Exception in source_show");
    }
}

static void jpegxs_source_hide(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    context->active = false;
    
    if (context->receive_thread.joinable()) {
        context->receive_thread.join();
    }
    
    if (context->audio_thread.joinable()) {
        context->audio_thread.join();
    }
    
    if (context->srt_transport) {
        context->srt_transport->stop();
        context->srt_transport.reset();
    }
    
    if (context->udp_socket) {
        context->udp_socket->close();
        context->udp_socket.reset();
    }
    
    if (context->audio_udp_socket) {
        context->audio_udp_socket->close();
        context->audio_udp_socket.reset();
    }
    
    context->rtp_depacketizer.reset();
    context->decoder.reset();
    
    blog(LOG_INFO, "[JPEG XS] Source stopped");
}

static uint32_t jpegxs_source_get_width(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    return context->width;
}

static uint32_t jpegxs_source_get_height(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    return context->height;
}

static obs_properties_t *jpegxs_source_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    
    obs_properties_t *props = obs_properties_create();
    
    // Main Transport Selection
    obs_property_t *p_mode = obs_properties_add_list(props, "transport_mode", "Transport Protocol", 
                                                     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(p_mode, "SRT", "SRT");
    obs_property_list_add_string(p_mode, "ST 2110-22 (UDP/Multicast)", "ST 2110-22 (UDP/Multicast)");
    
    obs_property_set_modified_callback(p_mode, transport_mode_modified);
    
    // Group: SRT Configuration
    obs_properties_t *srt_props = obs_properties_create();
    obs_properties_add_text(srt_props, "srt_url", "SRT Listen URL", OBS_TEXT_DEFAULT);
    obs_property_t *p_lat = obs_properties_add_int(srt_props, "srt_latency", "Latency (ms)", 20, 8000, 10);
    obs_property_set_long_description(p_lat, "SRT buffer latency. Lower values decrease delay but increase risk of dropouts.");
    obs_properties_add_text(srt_props, "srt_passphrase", "Passphrase", OBS_TEXT_PASSWORD);
    
    obs_properties_add_group(props, "group_srt", "SRT Configuration", OBS_GROUP_NORMAL, srt_props);
    
    // Group: ST 2110 / UDP Configuration
    obs_properties_t *udp_props = obs_properties_create();
    obs_properties_add_int(udp_props, "st2110_port", "UDP Port (Video)", 1024, 65535, 1);
    obs_properties_add_int(udp_props, "st2110_audio_port", "UDP Port (Audio)", 1024, 65535, 1);
    obs_properties_add_text(udp_props, "st2110_multicast_ip", "Multicast Group", OBS_TEXT_DEFAULT);
    obs_properties_add_text(udp_props, "st2110_interface_ip", "Interface IP", OBS_TEXT_DEFAULT);
    
    obs_properties_add_group(props, "group_st2110", "ST 2110 / UDP Configuration", OBS_GROUP_NORMAL, udp_props);
    
    // Group: Format & Decoding (SDP / Manual)
    obs_properties_t *fmt_props = obs_properties_create();
    
    obs_properties_add_path(fmt_props, "sdp_file_path", "SDP File (Optional)", OBS_PATH_FILE, "SDP Files (*.sdp);;All Files (*.*)", NULL);
    
    obs_properties_add_int(fmt_props, "manual_width", "Manual Width", 0, 8192, 1);
    obs_properties_add_int(fmt_props, "manual_height", "Manual Height", 0, 8192, 1);
    
    // Actually nested groups might be too much, just keep flat in Format group
    obs_properties_add_int(fmt_props, "manual_fps_num", "FPS Numerator", 0, 120000, 1);
    obs_properties_add_int(fmt_props, "manual_fps_den", "FPS Denominator", 0, 1001, 1);
    
    obs_properties_add_group(props, "group_format", "Format & Decoding", OBS_GROUP_NORMAL, fmt_props);

    // Group: Advanced
    obs_properties_t *adv_props = obs_properties_create();
    obs_property_t *p_thread = obs_properties_add_int(adv_props, "threads", "Decoder Threads", 0, 64, 1);
    obs_property_set_long_description(p_thread, "Set to 0 for auto-detection based on CPU cores.");
    
    obs_properties_add_group(props, "group_advanced", "Advanced", OBS_GROUP_NORMAL, adv_props);
    
    return props;
}

static void jpegxs_source_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "transport_mode", "SRT");
    obs_data_set_default_string(settings, "srt_url", "srt://0.0.0.0:9000");
    obs_data_set_default_int(settings, "srt_latency", 20);
    
    obs_data_set_default_int(settings, "st2110_port", 5000);
    obs_data_set_default_int(settings, "st2110_audio_port", 5002);
    obs_data_set_default_string(settings, "st2110_multicast_ip", "239.1.1.1");
    obs_data_set_default_string(settings, "st2110_interface_ip", "");
    
    obs_data_set_default_string(settings, "sdp_file_path", "");
    obs_data_set_default_int(settings, "manual_width", 1920);
    obs_data_set_default_int(settings, "manual_height", 1080);
    obs_data_set_default_int(settings, "manual_fps_num", 60000);
    obs_data_set_default_int(settings, "manual_fps_den", 1001);

    obs_data_set_default_int(settings, "threads", 0);
}
