/*
 * OBS JPEG XS Source Implementation
 * Handles OBS source callbacks and coordinates decoding/receiving
 */

#include "obs_jpegxs_source.h"
#include "jpegxs_decoder.h"
#include "../network/rtp_packet.h"
#include "../network/srt_transport.h"

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

#include <memory>
#include <atomic>
#include <thread>

using jpegxs::RTPDepacketizer;
using jpegxs::SRTTransport;

struct jpegxs_source {
    obs_source_t *source;
    
    // JPEG XS decoder
    std::unique_ptr<JpegXSDecoder> decoder;
    
    // Network transport
    std::unique_ptr<RTPDepacketizer> rtp_depacketizer;
    std::unique_ptr<SRTTransport> srt_transport;
    
    // Configuration
    uint32_t width;
    uint32_t height;
    std::string srt_url;
    std::string srt_passphrase;
    uint32_t srt_latency_ms;
    
    // Receive thread
    std::thread receive_thread;
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

void register_jpegxs_source(struct obs_source_info *info)
{
    info->id = "jpegxs_source";
    info->type = OBS_SOURCE_TYPE_INPUT;
    info->output_flags = OBS_SOURCE_ASYNC_VIDEO;
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
    return "JPEG XS Source (RTP/SRT)";
}

static void *jpegxs_source_create(obs_data_t *settings, obs_source_t *source)
{
    blog(LOG_INFO, "[JPEG XS] Creating source instance");
    
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

static void jpegxs_source_update(void *data, obs_data_t *settings)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    context->srt_url = obs_data_get_string(settings, "srt_url");
    context->srt_latency_ms = (uint32_t)obs_data_get_int(settings, "srt_latency");
    context->srt_passphrase = obs_data_get_string(settings, "srt_passphrase");
    
    blog(LOG_INFO, "[JPEG XS] Settings updated: %s, latency %u ms",
         context->srt_url.c_str(), context->srt_latency_ms);
}

static void receive_loop(jpegxs_source *context)
{
    blog(LOG_INFO, "[JPEG XS] Receive thread started");
    
    while (context->active) {
        // TODO: Implement receive loop
        // 1. Receive data from SRT
        // 2. Depacketize RTP
        // 3. Decode JPEG XS
        // 4. Output to OBS via obs_source_output_video()
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    blog(LOG_INFO, "[JPEG XS] Receive thread stopped");
}

static void jpegxs_source_show(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    blog(LOG_INFO, "[JPEG XS] Starting source");
    
    // Initialize decoder
    context->decoder = std::make_unique<JpegXSDecoder>();
    context->decoder->initialize();
    
    // Initialize RTP depacketizer
    context->rtp_depacketizer = std::make_unique<RTPDepacketizer>();
    
    // Initialize SRT transport
    // TODO: Parse SRT URL for port (srt://0.0.0.0:port)
    SRTTransport::Config srt_config;
    srt_config.mode = SRTTransport::Mode::LISTENER;
    srt_config.address = "0.0.0.0";
    srt_config.port = 9000;  // TODO: Extract from context->srt_url
    srt_config.latency_ms = context->srt_latency_ms;
    srt_config.passphrase = context->srt_passphrase;
    
    context->srt_transport = std::make_unique<SRTTransport>(srt_config);
    if (!context->srt_transport->start()) {
        blog(LOG_ERROR, "[JPEG XS] Failed to start SRT listener");
        return;
    }
    
    // Start receive thread
    context->active = true;
    context->receive_thread = std::thread(receive_loop, context);
    
    blog(LOG_INFO, "[JPEG XS] Source started");
}

static void jpegxs_source_hide(void *data)
{
    jpegxs_source *context = static_cast<jpegxs_source*>(data);
    
    blog(LOG_INFO, "[JPEG XS] Stopping source");
    
    context->active = false;
    
    if (context->receive_thread.joinable()) {
        context->receive_thread.join();
    }
    
    if (context->srt_transport) {
        context->srt_transport->stop();
        context->srt_transport.reset();
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
    
    obs_properties_add_text(props, "srt_url",
                           "SRT Listen URL (srt://0.0.0.0:port)",
                           OBS_TEXT_DEFAULT);
    
    obs_properties_add_int(props, "srt_latency",
                          "SRT Latency (ms)",
                          20, 8000, 10);
    
    obs_properties_add_text(props, "srt_passphrase",
                           "SRT Passphrase (decryption)",
                           OBS_TEXT_PASSWORD);
    
    return props;
}

static void jpegxs_source_get_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, "srt_url", "srt://0.0.0.0:9000");
    obs_data_set_default_int(settings, "srt_latency", 20);
    obs_data_set_default_string(settings, "srt_passphrase", "");
}
