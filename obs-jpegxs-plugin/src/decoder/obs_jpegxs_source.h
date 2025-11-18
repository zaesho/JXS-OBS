/*
 * OBS JPEG XS Source (Decoder)
 * Implements obs_source_info callbacks for JPEG XS receiving
 */

#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register JPEG XS source with OBS
 * @param info Source info structure to populate
 */
void register_jpegxs_source(struct obs_source_info *info);

#ifdef __cplusplus
}
#endif
