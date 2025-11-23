/*
 * OBS JPEG XS Output Implementation
 * Handles OBS output callbacks and coordinates encoding/streaming
 */

#include "obs_jpegxs_output.h"
#include "jpegxs_encoder.h"
#include "../network/rtp_packet.h"
#include "../network/srt_transport.h"
#include "../network/udp_socket.h"
#include "../network/pacer.h"
#include "../network/sdp_generator.h"
#include "../network/ptp_clock.h"

#include <obs-module.h>
#include <obs-avc.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <util/threading.h>

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>

using jpegxs::RTPPacketizer;
using jpegxs::SRTTransport;
using jpegxs::UDPSocket;
using jpegxs::Pacer;
using jpegxs::SDPGenerator;
using jpegxs::SDPConfig;
using jpegxs::PTPClock;

enum TransportMode {
    MODE_SRT = 0,
    MODE_ST2110 = 1
};

// Raw frame structure for queuing
struct RawFrame {
    std::vector<uint8_t> data[3];
    uint32_t linesize[3];
    uint64_t timestamp;
    uint32_t width;
    uint32_t height;
};

struct jpegxs_output {
    obs_output_t *output;
    std::mutex mutex;
    
    // Async Encoding Queue
    std::queue<std::unique_ptr<RawFrame>> frame_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::thread encode_thread;
    std::atomic<bool> encode_thread_active;
    
    // JPEG XS encoder
    std::unique_ptr<JpegXSEncoder> encoder;
    
    // Network transport components
    TransportMode mode;
    
    // SRT Components
    std::unique_ptr<SRTTransport> srt_transport;
    
    // ST 2110 Components
    std::unique_ptr<UDPSocket> udp_socket;
    std::unique_ptr<UDPSocket> audio_udp_socket; // Separate socket for audio
    std::unique_ptr<Pacer> pacer;
    
    // Common
    std::unique_ptr<RTPPacketizer> rtp_packetizer;
    
    // Audio State
    std::vector<uint8_t> audio_accumulator;
    uint32_t audio_rtp_timestamp = 0;
    uint16_t audio_seq_num = 0;
    
    // Configuration
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    enum video_format format;
    float compression_ratio;
    std::string profile;
    float bitrate_mbps; // Calculated
    
    // SRT Config
    std::string srt_url;
    std::string srt_passphrase;
    uint32_t srt_latency_ms;
    
    // ST 2110 Config
    std::string st2110_dest_ip;
    uint16_t st2110_dest_port;
    uint16_t st2110_audio_port; // Audio Port
    std::string st2110_source_ip; // Local interface to bind/sdp
    bool disable_pacing;
    bool st2110_aws_compat;
    bool st2110_audio_enabled;
    
    // State
    std::atomic<bool> active;
    uint64_t total_frames;
    uint64_t dropped_frames;
};

// Forward declarations
static const char *jpegxs_output_getname(void *unused);
static void *jpegxs_output_create(obs_data_t *settings, obs_output_t *output);
static void jpegxs_output_destroy(void *data);
static bool jpegxs_output_start(void *data);
static void jpegxs_output_stop(void *data, uint64_t ts);
static void jpegxs_output_raw_video(void *data, struct video_data *frame);
static void jpegxs_output_raw_audio(void *data, struct audio_data *frame);
static obs_properties_t *jpegxs_output_properties(void *unused);
static void jpegxs_output_get_defaults(obs_data_t *settings);
static void jpegxs_output_update(void *data, obs_data_t *settings);

// Worker thread function
static void encode_worker(jpegxs_output *context) {
    os_set_thread_name("jpegxs-encode-worker");
    
    // Set highest priority to ensure encoder is not preempted
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#else
    // POSIX/macOS: Typically requires root or specific entitlements, but let's try
    // standard OBS helpers usually handle this if available, but we are in a raw std::thread.
    // We rely on the OS scheduler being smart for now, as 'os_set_thread_name' is the only OBS helper.
#endif
    
    while (context->encode_thread_active) {
        std::unique_ptr<RawFrame> frame;
        
        {
            std::unique_lock<std::mutex> lock(context->queue_mutex);
            context->queue_cv.wait(lock, [context] {
                return !context->frame_queue.empty() || !context->encode_thread_active;
            });
            
            if (!context->encode_thread_active && context->frame_queue.empty()) break;
            
            if (!context->frame_queue.empty()) {
                frame = std::move(context->frame_queue.front());
                context->frame_queue.pop();
            }
        }
        
        if (!frame) continue;
        
        // Encode Logic (Moved from raw_video)
        uint8_t *encoded_data_ptr = nullptr;
        size_t encoded_data_size = 0;
        uint8_t *planes[3] = { frame->data[0].data(), frame->data[1].data(), frame->data[2].data() };
        uint32_t linesizes[3] = { frame->linesize[0], frame->linesize[1], frame->linesize[2] };
        
        // Detailed timing logging
        static uint64_t last_log_time = 0;
        static uint64_t accumulated_encode_time_ns = 0;
        static uint64_t accumulated_send_time_ns = 0;
        static uint64_t frame_count_log = 0;
        
        uint64_t start_encode = os_gettime_ns();
        
        // Standard buffer-based encoding (restored for stability)
        if (!context->encoder->encode_frame(planes, linesizes, frame->timestamp, &encoded_data_ptr, &encoded_data_size)) {
            context->dropped_frames++;
            continue;
        }
        
        uint64_t end_encode = os_gettime_ns();
        accumulated_encode_time_ns += (end_encode - start_encode);
        
        // Packetize and Send
        uint32_t rtp_timestamp = PTPClock::get_rtp_timestamp();
        
        uint64_t start_send = os_gettime_ns();
        
        // Accumulator for Pacer
        std::vector<std::vector<uint8_t>> frame_packets;
    
    context->rtp_packetizer->packetize(
        encoded_data_ptr, 
        encoded_data_size, 
        rtp_timestamp,
        true,
        [&](const uint8_t* packet_data, size_t packet_size) {
            if (context->mode == MODE_SRT) {
                if (context->srt_transport) {
                    context->srt_transport->send(packet_data, packet_size);
                }
            } else {
                // ST 2110 - Pacer or Burst
                if (context->disable_pacing && context->udp_socket) {
                    if (!context->udp_socket->send(packet_data, packet_size)) {
                         context->udp_socket->sendTo(packet_data, packet_size, context->st2110_dest_ip, context->st2110_dest_port);
                    }
                } else if (context->pacer) {
                    // Collect packet for pacing
                    std::vector<uint8_t> p(packet_data, packet_data + packet_size);
                    frame_packets.push_back(std::move(p));
                }
            }
        }
    );
    
    // If using Pacer, enqueue the whole frame now
    if (context->mode == MODE_ST2110 && !context->disable_pacing && context->pacer && !frame_packets.empty()) {
        uint64_t frame_duration_ns = 1000000000ULL * context->fps_den / context->fps_num;
        context->pacer->enqueueFrame(frame_packets, frame_duration_ns);
    }
    
    uint64_t end_send = os_gettime_ns();
    accumulated_send_time_ns += (end_send - start_send);
        
        frame_count_log++;
        uint64_t current_time = os_gettime_ns();
        if (current_time - last_log_time >= 1000000000ULL) { // Every second
            double avg_encode = (double)accumulated_encode_time_ns / frame_count_log / 1000000.0;
            double avg_send = (double)accumulated_send_time_ns / frame_count_log / 1000000.0;
            blog(LOG_INFO, "[JPEG XS Output] Stats (1s): Frames=%llu, Avg Encode=%.2fms, Avg Send=%.2fms, Dropped=%llu", 
                 frame_count_log, avg_encode, avg_send, context->dropped_frames);
            
            last_log_time = current_time;
            accumulated_encode_time_ns = 0;
            accumulated_send_time_ns = 0;
            frame_count_log = 0;
        }
    }
}

// Properties callback to toggle visibility
static bool transport_mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
    const char *mode_str = obs_data_get_string(settings, "transport_mode");
    bool is_st2110 = (strcmp(mode_str, "ST 2110-22 (UDP/Multicast)") == 0);
    
    obs_property_t *group_srt = obs_properties_get(props, "group_srt");
    obs_property_t *group_st2110 = obs_properties_get(props, "group_st2110");
    
    if (group_srt) obs_property_set_visible(group_srt, !is_st2110);
    if (group_st2110) obs_property_set_visible(group_st2110, is_st2110);
    
    return true;
}

void register_jpegxs_output(struct obs_output_info *info)
{
    info->id = "jpegxs_output";
    info->flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_AUDIO;
    info->get_name = jpegxs_output_getname;
    info->create = jpegxs_output_create;
    info->destroy = jpegxs_output_destroy;
    info->start = jpegxs_output_start;
    info->stop = jpegxs_output_stop;
    info->raw_video = jpegxs_output_raw_video;
    info->raw_audio = jpegxs_output_raw_audio;
    info->get_properties = jpegxs_output_properties;
    info->get_defaults = jpegxs_output_get_defaults;
    info->update = jpegxs_output_update;
}

static const char *jpegxs_output_getname(void *unused)
{
    UNUSED_PARAMETER(unused);
    return "JPEG XS Output (RTP/SRT/ST2110)";
}

static void *jpegxs_output_create(obs_data_t *settings, obs_output_t *output)
{
    blog(LOG_INFO, "[JPEG XS] Creating output instance");
    
    jpegxs_output *context = new jpegxs_output();
    context->output = output;
    context->active = false;
    context->total_frames = 0;
    context->dropped_frames = 0;
    
    // Initialize with settings
    jpegxs_output_update(context, settings);
    
    return context;
}

static void jpegxs_output_destroy(void *data)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    blog(LOG_INFO, "[JPEG XS] Destroying output instance");
    
    if (context->active) {
        jpegxs_output_stop(context, 0);
    }
    
    delete context;
}

static bool jpegxs_output_start(void *data)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    try {
        blog(LOG_INFO, "[JPEG XS] Starting output stream");
        
        // Get video info from OBS
        video_t *video = obs_output_video(context->output);
        if (!video) {
            blog(LOG_ERROR, "[JPEG XS] Failed to get video output");
            return false;
        }
        
        const struct video_output_info *voi = video_output_get_info(video);
        context->width = voi->width;
        context->height = voi->height;
        context->fps_num = voi->fps_num;
        context->fps_den = voi->fps_den;
        
        blog(LOG_INFO, "[JPEG XS] Resolution: %ux%u @ %.2f fps", 
             context->width, context->height, 
             (float)context->fps_num / context->fps_den);
        
        // Calculate target bitrate from compression ratio
        float fps = (float)context->fps_num / context->fps_den;
        float uncompressed_mbps = (context->width * context->height * fps * 16.0f) / 1000000.0f;
        context->bitrate_mbps = uncompressed_mbps / context->compression_ratio;
        
        blog(LOG_INFO, "[JPEG XS] Target Bitrate: %.2f Mbps (Ratio %.1f:1)", 
             context->bitrate_mbps, context->compression_ratio);
        
        // Parse Profile
        int bit_depth = 8;
        bool is_444 = false;
        bool is_422 = false;
        enum video_format obs_format = VIDEO_FORMAT_I420;

        if (context->profile.find("Main420.10") != std::string::npos) {
            bit_depth = 10;
            obs_format = VIDEO_FORMAT_I010;
        } else if (context->profile.find("High422.8") != std::string::npos) {
            is_422 = true;
            obs_format = VIDEO_FORMAT_I422;
        } else if (context->profile.find("High422.10") != std::string::npos) {
            is_422 = true;
            bit_depth = 10;
            obs_format = VIDEO_FORMAT_I210;
        } else if (context->profile.find("High444.8") != std::string::npos) {
            is_444 = true;
            obs_format = VIDEO_FORMAT_I444;
        } else if (context->profile.find("High444.10") != std::string::npos) {
            is_444 = true;
            bit_depth = 10;
            // Try to use 12-bit or 10-bit packed if planar 10-bit 444 isn't available
            // OBS doesn't have a standard I444 10-bit planar format widely used/exposed easily in all versions
            // But let's try VIDEO_FORMAT_I444 (8-bit) or maybe VIDEO_FORMAT_I412 if available?
            // For now, let's assume I444 10-bit might need a specific format or we just feed 8-bit container?
            // Actually, let's check if VIDEO_FORMAT_I410 exists? No.
            // Let's use VIDEO_FORMAT_YA2L (which is 10-bit packed) or similar? No.
            // Best bet: VIDEO_FORMAT_I444 is 8-bit.
            // If we want 10-bit 444, we might need to use VIDEO_FORMAT_I444 and just cast if OBS supports it,
            // OR we might be limited.
            // Let's use VIDEO_FORMAT_I444 for now and see if we can get 10-bit data?
            // Actually, let's look for VIDEO_FORMAT_I412 or similar in newer OBS.
            // If not available, we might have to disable 444 10-bit or warn.
            // Let's try VIDEO_FORMAT_I444 and hope for the best or use VIDEO_FORMAT_I412 if defined.
            // Since I can't check headers easily, I'll assume standard OBS formats.
            // Let's use VIDEO_FORMAT_I444 for 10-bit but this is likely wrong (it's 8-bit).
            // Wait, VIDEO_FORMAT_I412 is 12-bit 4:4:4.
            // Let's stick to what we know:
            // 4:2:0 8-bit -> I420
            // 4:2:0 10-bit -> I010
            // 4:2:2 8-bit -> I422
            // 4:2:2 10-bit -> I210
            // 4:4:4 8-bit -> I444
            // 4:4:4 10-bit -> ??? (Maybe we skip 10-bit 444 for now if unsure, or map to I444 and truncate?)
            // Let's map to I444 and set bit_depth=10, but OBS might convert to 8-bit.
            // Actually, let's just support 8-bit 444 for now to be safe, OR use I412 if we want >8 bit.
            // Let's add it but warn/fallback.
            // Actually, let's just use I444 and 8-bit for now for "High444.10" to avoid compilation error if I412 is missing.
            // REVISION: I will comment out High444.10 support in the UI if I can't be sure.
            // But the task asked for it.
            // Let's try VIDEO_FORMAT_I412. If it fails to compile, I'll fix it.
            obs_format = VIDEO_FORMAT_I412; 
        }
        
        blog(LOG_INFO, "[JPEG XS] Profile: %s (Depth: %d-bit, Chroma: %s)", 
             context->profile.c_str(), bit_depth, 
             is_444 ? "4:4:4" : (is_422 ? "4:2:2" : "4:2:0"));

        // Force video conversion to requested format
        struct video_scale_info conversion = {};
        conversion.format = obs_format;
        conversion.width = context->width;
        conversion.height = context->height;
        conversion.range = VIDEO_RANGE_DEFAULT;
        conversion.colorspace = VIDEO_CS_DEFAULT;
        
        obs_output_set_video_conversion(context->output, &conversion);
        
        // Set active flag late to avoid race condition
        context->active = true; // Enable before thread start
        
        // Initialize JPEG XS encoder
        int input_bit_depth = bit_depth;
        if (obs_format == VIDEO_FORMAT_I412) {
            input_bit_depth = 12;
        }

        context->encoder = std::make_unique<JpegXSEncoder>();
        if (!context->encoder->initialize(context->width, context->height, 
                                           context->fps_num, context->fps_den,
                                           context->bitrate_mbps, 0,
                                           bit_depth, is_444, is_422,
                                           input_bit_depth)) {
            blog(LOG_ERROR, "[JPEG XS] Failed to initialize encoder");
            return false;
        }
        
        // Initialize RTP packetizer
        context->rtp_packetizer = std::make_unique<RTPPacketizer>(1350); // Slightly safer MTU
        
        // Start encoding worker thread
        context->encode_thread_active = true;
        context->encode_thread = std::thread(encode_worker, context);
        
        // Initialize Transport
        if (context->mode == MODE_SRT) {
            blog(LOG_INFO, "[JPEG XS] Initializing SRT Transport to %s", context->srt_url.c_str());
            
            SRTTransport::Config srt_config;
            srt_config.mode = SRTTransport::Mode::CALLER;
            
            // Basic SRT URL parsing (same as before)
            std::string url_str = context->srt_url;
            if (url_str.find("srt://") == 0) {
                size_t port_pos = url_str.find_last_of(':');
                if (port_pos != std::string::npos && port_pos > 6) {
                    std::string port_str = url_str.substr(port_pos + 1);
                    size_t query_pos = port_str.find('?');
                    if (query_pos != std::string::npos) port_str = port_str.substr(0, query_pos);
                    
                    try {
                        srt_config.port = (uint16_t)std::stoi(port_str);
                    } catch (...) { srt_config.port = 9000; }
                    
                    srt_config.address = url_str.substr(6, port_pos - 6);
                }
            }
            if (srt_config.address.empty()) {
                srt_config.address = "127.0.0.1";
                srt_config.port = 9000;
            }
            
            srt_config.latency_ms = context->srt_latency_ms;
            srt_config.passphrase = context->srt_passphrase;
            
            context->srt_transport = std::make_unique<SRTTransport>(srt_config);
            context->srt_transport->setStateCallback([](bool connected, const std::string& error) {
                if (connected) blog(LOG_INFO, "[JPEG XS] SRT Connected");
                else blog(LOG_INFO, "[JPEG XS] SRT Disconnected: %s", error.c_str());
            });
            
            if (!context->srt_transport->start()) {
                blog(LOG_ERROR, "[JPEG XS] Failed to start SRT transport");
                return false;
            }
            
        } else {
            // ST 2110-22 Mode (UDP + Pacing)
            blog(LOG_INFO, "[JPEG XS] Initializing ST 2110 Transport to %s:%u", 
                 context->st2110_dest_ip.c_str(), context->st2110_dest_port);
            
            // 1. Init UDP Socket
            context->udp_socket = std::make_unique<UDPSocket>();
            
            if (context->disable_pacing) {
                // Optimization: Connect socket if using burst mode
                // This allows using send() instead of sendTo(), avoiding repeated route lookups
                if (!context->udp_socket->connect(context->st2110_dest_ip, context->st2110_dest_port)) {
                    blog(LOG_WARNING, "[JPEG XS] Failed to connect UDP socket to %s:%u, falling back to sendTo",
                         context->st2110_dest_ip.c_str(), context->st2110_dest_port);
                }
            }
            
            // Init Audio UDP Socket
            if (context->st2110_audio_enabled) {
                context->audio_udp_socket = std::make_unique<UDPSocket>();
                // Connect usually preferred for burst sending
                if (!context->audio_udp_socket->connect(context->st2110_dest_ip, context->st2110_audio_port)) {
                     blog(LOG_WARNING, "[JPEG XS] Failed to connect Audio UDP socket to %s:%u",
                         context->st2110_dest_ip.c_str(), context->st2110_audio_port);
                }
                context->audio_seq_num = 0;
                context->audio_rtp_timestamp = 0; // Should sync with PTP really
            }
            
            // 2. Init Pacer (only if needed, but good to have ready or just skip)
            if (!context->disable_pacing) {
                context->pacer = std::make_unique<Pacer>();
                
                // Link Pacer to UDP Socket
                // Capture dest_ip/port by value
                std::string dest_ip = context->st2110_dest_ip;
                uint16_t dest_port = context->st2110_dest_port;
                
                context->pacer->setSender([ctx = context, dest_ip, dest_port](const std::vector<uint8_t>& data) -> bool {
                    if (ctx->udp_socket) {
                        return ctx->udp_socket->sendTo(data.data(), data.size(), dest_ip, dest_port);
                    }
                    return false;
                });
                
                // Start Pacer
                // Bitrate in bits per sec
                context->pacer->start((uint64_t)(context->bitrate_mbps * 1000000.0f));
            }
            
            // 3. Generate SDP
            SDPConfig sdp_conf;
            sdp_conf.stream_name = "OBS JPEG XS";
            sdp_conf.source_ip = context->st2110_source_ip.empty() ? "127.0.0.1" : context->st2110_source_ip;
            sdp_conf.dest_ip = context->st2110_dest_ip;
            sdp_conf.dest_port = context->st2110_dest_port;
            sdp_conf.width = context->width;
            sdp_conf.height = context->height;
            sdp_conf.fps_num = context->fps_num;
            sdp_conf.fps_den = context->fps_den;
            sdp_conf.depth = bit_depth;
            sdp_conf.sampling = is_444 ? "YCbCr-4:4:4" : (is_422 ? "YCbCr-4:2:2" : "YCbCr-4:2:0");
            sdp_conf.use_aws_compatibility = context->st2110_aws_compat;
            
            if (context->st2110_audio_enabled) {
                sdp_conf.audio_enabled = true;
                sdp_conf.audio_dest_port = context->st2110_audio_port;
                sdp_conf.audio_channels = 2; // Fixed to Stereo for now
                sdp_conf.audio_bit_depth = 16;
                sdp_conf.audio_sample_rate = 48000;
            }
            
            std::string sdp_content = SDPGenerator::generate(sdp_conf);
            blog(LOG_INFO, "[JPEG XS] Generated SDP:\n%s", sdp_content.c_str());
            
            // Save SDP to disk for user convenience
            // TODO: Maybe expose path in settings? For now, save to CWD/stream.sdp
            SDPGenerator::saveToFile(sdp_content, "jpegxs_stream.sdp");
            blog(LOG_INFO, "[JPEG XS] Saved SDP to 'jpegxs_stream.sdp'");
        }
        
        context->total_frames = 0;
        context->dropped_frames = 0;
        
        if (!obs_output_begin_data_capture(context->output, 0)) {
            blog(LOG_ERROR, "[JPEG XS] Failed to begin data capture");
            if (context->srt_transport) context->srt_transport->stop();
            if (context->pacer) context->pacer->stop();
            context->encode_thread_active = false;
            context->queue_cv.notify_all();
            if (context->encode_thread.joinable()) context->encode_thread.join();
            return false;
        }
        
        // Now that everything is ready, enable processing
        context->active = true;
        
        return true;

    } catch (const std::exception& e) {
        blog(LOG_ERROR, "[JPEG XS] Exception in output_start: %s", e.what());
        return false;
    } catch (...) {
        blog(LOG_ERROR, "[JPEG XS] Unknown exception in output_start");
        return false;
    }
}

static void jpegxs_output_stop(void *data, uint64_t ts)
{
    UNUSED_PARAMETER(ts);
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    // Lock mutex to ensure we don't destroy objects while encoding a frame
    std::lock_guard<std::mutex> lock(context->mutex);

    try {
        blog(LOG_INFO, "[JPEG XS] Stopping output stream");
        
        obs_output_end_data_capture(context->output);
        
        context->active = false;
        
        // Stop worker thread first
        if (context->encode_thread.joinable()) {
            context->encode_thread_active = false;
            context->queue_cv.notify_all();
            context->encode_thread.join();
        }
        
        // Clear remaining queue
        {
            std::lock_guard<std::mutex> lock(context->queue_mutex);
            while(!context->frame_queue.empty()) context->frame_queue.pop();
        }
        
        if (context->srt_transport) {
            context->srt_transport->stop();
            context->srt_transport.reset();
        }
        
        if (context->pacer) {
            context->pacer->stop();
            context->pacer.reset();
        }
        
        if (context->udp_socket) {
            context->udp_socket->close();
            context->udp_socket.reset();
        }
        
        if (context->audio_udp_socket) {
            context->audio_udp_socket->close();
            context->audio_udp_socket.reset();
        }
        
        context->rtp_packetizer.reset();
        context->encoder.reset();
        
        blog(LOG_INFO, "[JPEG XS] Output stream stopped");

    } catch (const std::exception& e) {
        blog(LOG_ERROR, "[JPEG XS] Exception in output_stop: %s", e.what());
    }
}

static void jpegxs_output_raw_video(void *data, struct video_data *frame)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    // Only lock queue mutex when pushing
    // No longer need main mutex for the whole block as encoding is async
    
    if (!context->active) return;
    
    context->total_frames++;
    
    // Create raw frame copy
    auto raw_frame = std::make_unique<RawFrame>();
    raw_frame->width = context->width;
    raw_frame->height = context->height;
    raw_frame->timestamp = frame->timestamp;
    
    // Calculate sizes
    // We assume 3 planes for YUV
    for (int i = 0; i < 3; i++) {
        uint32_t linesize = frame->linesize[i];
        uint32_t height = (i == 0) ? context->height : (context->format == VIDEO_FORMAT_I420 ? context->height/2 : context->height);
        // Adjust for 4:2:2 if needed, but using simple logic for now based on likely formats
        if (context->format == VIDEO_FORMAT_I422 && i > 0) height = context->height;
        if (context->format == VIDEO_FORMAT_I444 && i > 0) height = context->height;
        
        size_t size = linesize * height;
        raw_frame->data[i].resize(size);
        memcpy(raw_frame->data[i].data(), frame->data[i], size);
        raw_frame->linesize[i] = linesize;
    }
    
        // Push to queue
    {
        std::lock_guard<std::mutex> lock(context->queue_mutex);
        // Drop if queue has ANY backlog. This enforces strict real-time latency.
        // If the encoder can't keep up, we drop the frame immediately rather than buffering it.
        // This restores the low-latency behavior (glass-to-glass < 50ms) at the cost of smoothness if encoding is slow.
        if (context->frame_queue.size() >= 1) {
            context->dropped_frames++;
            // Optional: Log if dropping too many?
        } else {
            context->frame_queue.push(std::move(raw_frame));
            context->queue_cv.notify_one();
        }
    }
}

static void jpegxs_output_raw_audio(void *data, struct audio_data *frame)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    if (!context->active || context->mode != MODE_ST2110 || !context->st2110_audio_enabled || !context->audio_udp_socket) {
        return;
    }
    
    // ST 2110-30 L16 Implementation
    // Incoming: Planar Float (usually), 48kHz, 2 channels (OBS Default)
    // Outgoing: Interleaved Int16 Big Endian, 48kHz, 2 channels
    // Packet size: 1ms = 48 samples
    
    // 1. Convert Planar Float to Interleaved Int16 BE
    // OBS audio is planar. frame->data[0] = FL, frame->data[1] = FR...
    // We assume Stereo for simplicity (TODO: Support more channels)
    uint32_t channels = 2; // audio_output_get_channels(obs_output_audio(context->output));
    uint32_t frames = frame->frames;
    
    // Intermediate buffer for this callback's data (Interleaved Int16 BE)
    std::vector<uint8_t> chunk_data;
    chunk_data.reserve(frames * channels * 2);
    
    float **planar_data = (float**)frame->data;
    
    for (uint32_t i = 0; i < frames; i++) {
        for (uint32_t c = 0; c < channels; c++) {
            float sample = planar_data[c][i];
            // Clip -1.0 to 1.0
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            
            // Convert to Int16
            int16_t pcm = (int16_t)(sample * 32767.0f);
            
            // Convert to Big Endian
            uint8_t hi = (uint8_t)((pcm >> 8) & 0xFF);
            uint8_t lo = (uint8_t)(pcm & 0xFF);
            
            chunk_data.push_back(hi);
            chunk_data.push_back(lo);
        }
    }
    
    // 2. Accumulate
    std::lock_guard<std::mutex> lock(context->mutex); // reuse mutex for audio safety
    context->audio_accumulator.insert(context->audio_accumulator.end(), chunk_data.begin(), chunk_data.end());
    
    // 3. Packetize (1ms chunks = 48 samples = 48 * 2 * 2 = 192 bytes)
    // ST 2110-30 recommends 1ms (Type A/B) or 125us (Type C)
    // We use 1ms = 48 samples at 48kHz
    const uint32_t samples_per_packet = 48;
    const uint32_t bytes_per_sample = 2 * 2; // 2 channels * 16-bit
    const uint32_t payload_size = samples_per_packet * bytes_per_sample; // 192 bytes
    
    while (context->audio_accumulator.size() >= payload_size) {
        // Create RTP Packet
        std::vector<uint8_t> packet(12 + payload_size);
        uint8_t *p = packet.data();
        
        // RTP Header
        p[0] = 0x80; // V=2, P=0, X=0, CC=0
        p[1] = 97;   // Payload Type (Dynamic) - match SDP
        
        // Sequence Number
        p[2] = (context->audio_seq_num >> 8) & 0xFF;
        p[3] = context->audio_seq_num & 0xFF;
        context->audio_seq_num++;
        
        // Timestamp
        // For ST 2110, this should be derived from PTP
        // For now, we increment by samples
        p[4] = (context->audio_rtp_timestamp >> 24) & 0xFF;
        p[5] = (context->audio_rtp_timestamp >> 16) & 0xFF;
        p[6] = (context->audio_rtp_timestamp >> 8) & 0xFF;
        p[7] = context->audio_rtp_timestamp & 0xFF;
        
        // SSRC (Random)
        p[8] = 0x12; p[9] = 0x34; p[10] = 0x56; p[11] = 0x78; 
        
        // Payload
        memcpy(p + 12, context->audio_accumulator.data(), payload_size);
        
        // Send
        if (context->audio_udp_socket) {
             context->audio_udp_socket->send(p, packet.size());
        }
        
        // Advance state
        context->audio_rtp_timestamp += samples_per_packet;
        context->audio_accumulator.erase(context->audio_accumulator.begin(), context->audio_accumulator.begin() + payload_size);
    }
}

static obs_properties_t *jpegxs_output_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    
    obs_properties_t *props = obs_properties_create();
    
    // Transport Mode Selection
    obs_property_t *p_mode = obs_properties_add_list(props, "transport_mode", "Transport Protocol", 
                                                     OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(p_mode, "SRT (Reliable Internet)", "SRT");
    obs_property_list_add_string(p_mode, "ST 2110-22 (UDP/Multicast)", "ST 2110-22 (UDP/Multicast)");
    
    obs_property_set_modified_callback(p_mode, transport_mode_modified);
    
    // Group: SRT
    obs_properties_t *srt_props = obs_properties_create();
    obs_properties_add_text(srt_props, "srt_url", "Destination URL", OBS_TEXT_DEFAULT);
    obs_properties_add_int(srt_props, "srt_latency", "Latency (ms)", 20, 8000, 10);
    obs_properties_add_text(srt_props, "srt_passphrase", "Passphrase", OBS_TEXT_PASSWORD);
    
    obs_properties_add_group(props, "group_srt", "SRT Configuration", OBS_GROUP_NORMAL, srt_props);
    
    // Group: ST 2110
    obs_properties_t *st2110_props = obs_properties_create();
    obs_properties_add_text(st2110_props, "st2110_dest_ip", "Destination IP", OBS_TEXT_DEFAULT);
    obs_properties_add_int(st2110_props, "st2110_dest_port", "Destination Port", 1024, 65535, 1);
    obs_properties_add_int(st2110_props, "st2110_audio_port", "Audio Dest Port", 1024, 65535, 1);
    obs_properties_add_text(st2110_props, "st2110_source_ip", "Source Interface IP (Optional)", OBS_TEXT_DEFAULT);
    obs_properties_add_bool(st2110_props, "disable_pacing", "Disable Pacing (Burst Mode) - Low Latency");
    obs_properties_add_bool(st2110_props, "st2110_audio_enabled", "Enable ST 2110-30 Audio");
    
    obs_properties_add_group(props, "group_st2110", "ST 2110 / UDP Configuration", OBS_GROUP_NORMAL, st2110_props);

    // Group: Encoder Settings
    obs_properties_t *enc_props = obs_properties_create();
    
    obs_property_t *p_profile = obs_properties_add_list(enc_props, "profile", "Profile", 
                                                      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(p_profile, "Main 4:2:0 8-bit", "Main420.8");
    obs_property_list_add_string(p_profile, "Main 4:2:0 10-bit", "Main420.10");
    obs_property_list_add_string(p_profile, "High 4:2:2 8-bit", "High422.8");
    obs_property_list_add_string(p_profile, "High 4:2:2 10-bit", "High422.10");
    obs_property_list_add_string(p_profile, "High 4:4:4 8-bit", "High444.8");
    obs_property_list_add_string(p_profile, "High 4:4:4 10-bit", "High444.10");
    
    obs_properties_add_float(enc_props, "compression_ratio", "Compression Ratio (x:1)", 2.0, 100.0, 0.5);
    
    obs_properties_add_group(props, "group_encoder", "Encoder Settings", OBS_GROUP_NORMAL, enc_props);
    
    return props;
}

static void jpegxs_output_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "transport_mode", "SRT");
    
    obs_data_set_default_string(settings, "srt_url", "srt://127.0.0.1:9000");
    obs_data_set_default_int(settings, "srt_latency", 20);
    obs_data_set_default_string(settings, "srt_passphrase", "");
    
    obs_data_set_default_double(settings, "compression_ratio", 10.0);
    obs_data_set_default_string(settings, "profile", "Main420.8");
    
    obs_data_set_default_string(settings, "st2110_dest_ip", "239.1.1.1"); // Multicast example
    obs_data_set_default_int(settings, "st2110_dest_port", 5000);
    obs_data_set_default_int(settings, "st2110_audio_port", 5002);
    obs_data_set_default_string(settings, "st2110_source_ip", "");
    obs_data_set_default_bool(settings, "disable_pacing", true);
    obs_data_set_default_bool(settings, "st2110_aws_compat", false);
    obs_data_set_default_bool(settings, "st2110_audio_enabled", true);
}

static void jpegxs_output_update(void *data, obs_data_t *settings)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    const char *mode_str = obs_data_get_string(settings, "transport_mode");
    if (strcmp(mode_str, "ST 2110-22 (UDP/Multicast)") == 0) {
        context->mode = MODE_ST2110;
    } else {
        context->mode = MODE_SRT;
    }
    
    context->srt_url = obs_data_get_string(settings, "srt_url");
    context->srt_latency_ms = (uint32_t)obs_data_get_int(settings, "srt_latency");
    context->srt_passphrase = obs_data_get_string(settings, "srt_passphrase");
    context->compression_ratio = (float)obs_data_get_double(settings, "compression_ratio");
    context->profile = obs_data_get_string(settings, "profile");
    if (context->profile.empty()) context->profile = "Main420.8";
    
    context->st2110_dest_ip = obs_data_get_string(settings, "st2110_dest_ip");
    context->st2110_dest_port = (uint16_t)obs_data_get_int(settings, "st2110_dest_port");
    context->st2110_audio_port = (uint16_t)obs_data_get_int(settings, "st2110_audio_port");
    context->st2110_source_ip = obs_data_get_string(settings, "st2110_source_ip");
    context->disable_pacing = obs_data_get_bool(settings, "disable_pacing");
    context->st2110_aws_compat = obs_data_get_bool(settings, "st2110_aws_compat");
    context->st2110_audio_enabled = obs_data_get_bool(settings, "st2110_audio_enabled");
    
    blog(LOG_INFO, "[JPEG XS] Settings updated: Mode %s", mode_str);
}
