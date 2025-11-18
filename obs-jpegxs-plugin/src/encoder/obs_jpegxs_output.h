/*
 * OBS JPEG XS Output
 * Implements obs_output_info callbacks for JPEG XS streaming
 */

#pragma once

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register JPEG XS output with OBS
 * @param info Output info structure to populate
 */
void register_jpegxs_output(struct obs_output_info *info);

#ifdef __cplusplus
}
#endif
