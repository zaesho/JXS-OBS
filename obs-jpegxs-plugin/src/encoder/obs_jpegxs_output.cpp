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

struct jpegxs_output {
    obs_output_t *output;
    
    // JPEG XS encoder
    std::unique_ptr<JpegXSEncoder> encoder;
    
    // Network transport components
    TransportMode mode;
    
    // SRT Components
    std::unique_ptr<SRTTransport> srt_transport;
    
    // ST 2110 Components
    std::unique_ptr<UDPSocket> udp_socket;
    std::unique_ptr<Pacer> pacer;
    
    // Common
    std::unique_ptr<RTPPacketizer> rtp_packetizer;
    
    // Configuration
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    enum video_format format;
    float compression_ratio;
    float bitrate_mbps; // Calculated
    
    // SRT Config
    std::string srt_url;
    std::string srt_passphrase;
    uint32_t srt_latency_ms;
    
    // ST 2110 Config
    std::string st2110_dest_ip;
    uint16_t st2110_dest_port;
    std::string st2110_source_ip; // Local interface to bind/sdp
    
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

// Properties callback to toggle visibility
static bool transport_mode_modified(obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
    const char *mode_str = obs_data_get_string(settings, "transport_mode");
    bool is_st2110 = (strcmp(mode_str, "ST 2110-22 (UDP/Multicast)") == 0);
    
    obs_property_set_visible(obs_properties_get(props, "srt_url"), !is_st2110);
    obs_property_set_visible(obs_properties_get(props, "srt_latency"), !is_st2110);
    obs_property_set_visible(obs_properties_get(props, "srt_passphrase"), !is_st2110);
    
    obs_property_set_visible(obs_properties_get(props, "st2110_dest_ip"), is_st2110);
    obs_property_set_visible(obs_properties_get(props, "st2110_dest_port"), is_st2110);
    obs_property_set_visible(obs_properties_get(props, "st2110_source_ip"), is_st2110);
    
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
        
        // Force video conversion to I420 (Software encoder constraint for now)
        struct video_scale_info conversion = {};
        conversion.format = VIDEO_FORMAT_I420;
        conversion.width = context->width;
        conversion.height = context->height;
        conversion.range = VIDEO_RANGE_DEFAULT;
        conversion.colorspace = VIDEO_CS_DEFAULT;
        
        obs_output_set_video_conversion(context->output, &conversion);
        
        // Set active flag
        context->active = true;

        // Initialize JPEG XS encoder
        context->encoder = std::make_unique<JpegXSEncoder>();
        if (!context->encoder->initialize(context->width, context->height, 
                                           context->fps_num, context->fps_den,
                                           context->bitrate_mbps)) {
            blog(LOG_ERROR, "[JPEG XS] Failed to initialize encoder");
            return false;
        }
        
        // Initialize RTP packetizer
        context->rtp_packetizer = std::make_unique<RTPPacketizer>(1350); // Slightly safer MTU
        
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
            // If multicast, we might need to set interface/TTL, but standard socket is ok for sending unicast/multicast usually
            // Just ensure we bind if we want a specific source IP
            
            // 2. Init Pacer
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
            return false;
        }
        
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
    
    try {
        blog(LOG_INFO, "[JPEG XS] Stopping output stream");
        
        obs_output_end_data_capture(context->output);
        
        context->active = false;
        
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
    
    try {
        if (!context->active || !context->encoder)
            return;
        
        context->total_frames++;
        
        // Encode frame
        std::vector<uint8_t> encoded_data;
        uint8_t *planes[3] = { frame->data[0], frame->data[1], frame->data[2] };
        uint32_t linesizes[3] = { frame->linesize[0], frame->linesize[1], frame->linesize[2] };
        
        if (!context->encoder->encode_frame(planes, linesizes, frame->timestamp, encoded_data)) {
            context->dropped_frames++;
            return;
        }
        
        // Determine timestamp
        // For ST 2110, use PTP clock
        // For SRT, use counter or PTP. Let's use PTP for both for consistency, 
        // though SRT uses internal timing. 
        // RTP packet timestamp should be 90kHz.
        uint32_t rtp_timestamp = PTPClock::get_rtp_timestamp();
        
        // Packetize
        auto rtp_packets = context->rtp_packetizer->packetize(
            encoded_data.data(), 
            encoded_data.size(), 
            rtp_timestamp,
            true
        );
        
        if (rtp_packets.empty()) {
            context->dropped_frames++;
            return;
        }
        
        if (context->mode == MODE_SRT) {
            if (context->srt_transport) {
                for (const auto& packet : rtp_packets) {
                    std::vector<uint8_t> serialized = packet->serialize();
                    context->srt_transport->send(serialized.data(), serialized.size());
                }
            }
        } else {
            // ST 2110 - Pacer
            if (context->pacer) {
                std::vector<std::vector<uint8_t>> serialized_packets;
                serialized_packets.reserve(rtp_packets.size());
                
                for (const auto& packet : rtp_packets) {
                    serialized_packets.push_back(packet->serialize());
                }
                
                // Calculate frame duration in ns for pacing
                // e.g. 60fps = 16666666 ns
                uint64_t frame_ns = (uint64_t)((double)context->fps_den / context->fps_num * 1000000000.0);
                
                context->pacer->enqueueFrame(serialized_packets, frame_ns);
            }
        }
        
        if (context->total_frames % 60 == 0) {
            blog(LOG_INFO, "[JPEG XS] Frame %llu encoded & sent", context->total_frames);
        }

    } catch (...) {
        // Catch all
    }
}

static void jpegxs_output_raw_audio(void *data, struct audio_data *frame)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(frame);
    // TODO: Implement Audio for ST 2110-30
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
    
    // Common
    obs_properties_add_float(props, "compression_ratio", "Compression Ratio (x:1)", 2.0, 100.0, 0.5);
    
    // SRT Properties
    obs_properties_add_text(props, "srt_url", "SRT Destination URL", OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "srt_latency", "SRT Latency (ms)", 20, 8000, 10);
    obs_properties_add_text(props, "srt_passphrase", "SRT Passphrase", OBS_TEXT_PASSWORD);
    
    // ST 2110 Properties
    obs_properties_add_text(props, "st2110_dest_ip", "ST 2110 Destination IP", OBS_TEXT_DEFAULT);
    obs_properties_add_int(props, "st2110_dest_port", "ST 2110 Destination Port", 1024, 65535, 1);
    obs_properties_add_text(props, "st2110_source_ip", "Source Interface IP (optional)", OBS_TEXT_DEFAULT);
    
    return props;
}

static void jpegxs_output_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "transport_mode", "SRT");
    
    obs_data_set_default_string(settings, "srt_url", "srt://127.0.0.1:9000");
    obs_data_set_default_int(settings, "srt_latency", 20);
    obs_data_set_default_string(settings, "srt_passphrase", "");
    
    obs_data_set_default_double(settings, "compression_ratio", 10.0);
    
    obs_data_set_default_string(settings, "st2110_dest_ip", "239.1.1.1"); // Multicast example
    obs_data_set_default_int(settings, "st2110_dest_port", 5000);
    obs_data_set_default_string(settings, "st2110_source_ip", "");
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
    
    context->st2110_dest_ip = obs_data_get_string(settings, "st2110_dest_ip");
    context->st2110_dest_port = (uint16_t)obs_data_get_int(settings, "st2110_dest_port");
    context->st2110_source_ip = obs_data_get_string(settings, "st2110_source_ip");
    
    blog(LOG_INFO, "[JPEG XS] Settings updated: Mode %s", mode_str);
}
