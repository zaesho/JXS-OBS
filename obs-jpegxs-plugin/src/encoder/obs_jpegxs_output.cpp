/*
 * OBS JPEG XS Output Implementation
 * Handles OBS output callbacks and coordinates encoding/streaming
 */

#include "obs_jpegxs_output.h"
#include "jpegxs_encoder.h"
#include "../network/rtp_packet.h"
#include "../network/srt_transport.h"

#include <obs-module.h>
#include <obs-avc.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <util/threading.h>

#include <memory>
#include <atomic>

using jpegxs::RTPPacketizer;
using jpegxs::SRTTransport;

struct jpegxs_output {
    obs_output_t *output;
    
    // JPEG XS encoder
    std::unique_ptr<JpegXSEncoder> encoder;
    
    // Network transport
    std::unique_ptr<RTPPacketizer> rtp_packetizer;
    std::unique_ptr<SRTTransport> srt_transport;
    
    // Configuration
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    float bitrate_mbps;
    std::string srt_url;
    std::string srt_passphrase;
    uint32_t srt_latency_ms;
    
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
static obs_properties_t *jpegxs_output_properties(void *unused);
static void jpegxs_output_get_defaults(obs_data_t *settings);
static void jpegxs_output_update(void *data, obs_data_t *settings);

void register_jpegxs_output(struct obs_output_info *info)
{
    info->id = "jpegxs_output";
    info->flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
    info->get_name = jpegxs_output_getname;
    info->create = jpegxs_output_create;
    info->destroy = jpegxs_output_destroy;
    info->start = jpegxs_output_start;
    info->stop = jpegxs_output_stop;
    info->raw_video = jpegxs_output_raw_video;
    info->get_properties = jpegxs_output_properties;
    info->get_defaults = jpegxs_output_get_defaults;
    info->update = jpegxs_output_update;
}

static const char *jpegxs_output_getname(void *unused)
{
    UNUSED_PARAMETER(unused);
    return "JPEG XS Output (RTP/SRT)";
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
    
    blog(LOG_INFO, "[JPEG XS] Starting output stream");
    blog(LOG_INFO, "[JPEG XS] Resolution: %ux%u @ %.2f fps", 
         context->width, context->height, 
         (float)context->fps_num / context->fps_den);
    blog(LOG_INFO, "[JPEG XS] Bitrate: %.2f Mbps", context->bitrate_mbps);
    blog(LOG_INFO, "[JPEG XS] SRT URL: %s", context->srt_url.c_str());
    
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
    
    // Initialize JPEG XS encoder
    context->encoder = std::make_unique<JpegXSEncoder>();
    if (!context->encoder->initialize(context->width, context->height, 
                                       context->fps_num, context->fps_den,
                                       context->bitrate_mbps)) {
        blog(LOG_ERROR, "[JPEG XS] Failed to initialize encoder");
        return false;
    }
    
    // Initialize RTP packetizer
    context->rtp_packetizer = std::make_unique<RTPPacketizer>(1400); // MTU
    
    // Initialize SRT transport
    // TODO: Parse SRT URL properly (srt://host:port)
    SRTTransport::Config srt_config;
    srt_config.mode = SRTTransport::Mode::CALLER;
    srt_config.address = "127.0.0.1";  // TODO: Extract from context->srt_url
    srt_config.port = 9000;            // TODO: Extract from context->srt_url
    srt_config.latency_ms = context->srt_latency_ms;
    srt_config.passphrase = context->srt_passphrase;
    
    context->srt_transport = std::make_unique<SRTTransport>(srt_config);
    if (!context->srt_transport->start()) {
        blog(LOG_ERROR, "[JPEG XS] Failed to start SRT transport");
        return false;
    }
    
    context->active = true;
    context->total_frames = 0;
    context->dropped_frames = 0;
    
    blog(LOG_INFO, "[JPEG XS] Output stream started successfully");
    
    return true;
}

static void jpegxs_output_stop(void *data, uint64_t ts)
{
    UNUSED_PARAMETER(ts);
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    blog(LOG_INFO, "[JPEG XS] Stopping output stream");
    blog(LOG_INFO, "[JPEG XS] Total frames: %llu, Dropped: %llu", 
         context->total_frames, context->dropped_frames);
    
    context->active = false;
    
    if (context->srt_transport) {
        context->srt_transport->stop();
        context->srt_transport.reset();
    }
    
    context->rtp_packetizer.reset();
    context->encoder.reset();
    
    blog(LOG_INFO, "[JPEG XS] Output stream stopped");
}

static void jpegxs_output_raw_video(void *data, struct video_data *frame)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    if (!context->active) {
        return;
    }
    
    context->total_frames++;
    
    // TODO: Implement encoding and streaming
    // 1. Convert video_data to encoder input format
    // 2. Encode frame with JPEG XS encoder
    // 3. Packetize into RTP packets
    // 4. Send via SRT transport
    
    blog(LOG_DEBUG, "[JPEG XS] Received frame %llu", context->total_frames);
}

static obs_properties_t *jpegxs_output_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    
    obs_properties_t *props = obs_properties_create();
    
    // SRT connection settings
    obs_properties_add_text(props, "srt_url", 
                           "SRT Destination URL (srt://host:port)", 
                           OBS_TEXT_DEFAULT);
    
    obs_properties_add_int(props, "srt_latency", 
                          "SRT Latency (ms)", 
                          20, 8000, 10);
    
    obs_properties_add_text(props, "srt_passphrase", 
                           "SRT Passphrase (encryption)", 
                           OBS_TEXT_PASSWORD);
    
    // JPEG XS encoding settings
    obs_properties_add_float(props, "bitrate_mbps", 
                            "Bitrate (Mbps)", 
                            100.0, 2000.0, 50.0);
    
    obs_properties_add_text(props, "profile", 
                           "Profile (High444.12, Main420.10, etc.)", 
                           OBS_TEXT_DEFAULT);
    
    return props;
}

static void jpegxs_output_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "srt_url", "srt://127.0.0.1:9000");
    obs_data_set_default_int(settings, "srt_latency", 20);
    obs_data_set_default_string(settings, "srt_passphrase", "");
    obs_data_set_default_double(settings, "bitrate_mbps", 600.0);
    obs_data_set_default_string(settings, "profile", "Main420.10");
}

static void jpegxs_output_update(void *data, obs_data_t *settings)
{
    jpegxs_output *context = static_cast<jpegxs_output*>(data);
    
    context->srt_url = obs_data_get_string(settings, "srt_url");
    context->srt_latency_ms = (uint32_t)obs_data_get_int(settings, "srt_latency");
    context->srt_passphrase = obs_data_get_string(settings, "srt_passphrase");
    context->bitrate_mbps = (float)obs_data_get_double(settings, "bitrate_mbps");
    
    blog(LOG_INFO, "[JPEG XS] Settings updated: %s @ %.0f Mbps, latency %u ms",
         context->srt_url.c_str(), context->bitrate_mbps, context->srt_latency_ms);
}
