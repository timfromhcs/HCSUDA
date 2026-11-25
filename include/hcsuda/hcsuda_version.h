#pragma once

/*
 * HCSUDA — HCS Unified Compute Runtime
 * Version and ABI identity.
 *
 * HCS_API_VERSION is the stable C ABI version. It only changes when an
 * existing struct layout or function signature changes incompatibly.
 */

#define HCS_VERSION_MAJOR 0
#define HCS_VERSION_MINOR 1
#define HCS_VERSION_PATCH 0

#define HCS_VERSION_STRING "0.1.0"
#define HCS_API_VERSION 1u
#define HCS_MAX_DEVICES 16

/* Product identity strings. Kept in one place so diagnostics stay consistent. */
#define HCS_PRODUCT_NAME "HCSUDA"
#define HCS_PRODUCT_FULL_NAME "HCS Unified Compute Runtime"
#define HCS_PRODUCT_TAGLINE "Made with love by HCSMedia & timfromhcs"
