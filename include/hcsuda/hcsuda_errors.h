#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable error model. Every public entry point returns an hcsResult and, on
 * failure, records structured context retrievable with hcsGetLastError().
 */
typedef enum hcsResult {
    HCS_SUCCESS = 0,
    HCS_E_INVALID_ARGUMENT = 1,
    HCS_E_NOT_INITIALIZED = 2,
    HCS_E_ALREADY_INITIALIZED = 3,
    HCS_E_NO_DEVICE = 4,
    HCS_E_UNSUPPORTED_API_VERSION = 5,
    HCS_E_UNSUPPORTED_FEATURE = 6,
    HCS_E_OUT_OF_MEMORY = 7,
    HCS_E_DEVICE_LOST = 8,
    HCS_E_SHADER_COMPILE = 9,
    HCS_E_SPIRV_INVALID = 10,
    HCS_E_KERNEL_NOT_FOUND = 11,
    HCS_E_TIMEOUT = 12,
    HCS_E_IO = 13,
    HCS_E_MODEL_CORRUPT = 14,
    HCS_E_TRANSLATION_UNSUPPORTED = 15,
    HCS_E_SECURITY_POLICY = 16,
    HCS_E_INTERNAL = 17
} hcsResult;

typedef struct hcsErrorInfo {
    uint32_t struct_size;
    int32_t code;
    int32_t backend_result;
    uint32_t reserved;
    char message[256];
    char operation[64];
    char correlation_id[40];
} hcsErrorInfo;

/* Returns a static, human-readable name for an error code. Never NULL. */
const char* hcsResultString(hcsResult result);

/* Fills the caller's struct with the calling thread's last error. */
hcsResult hcsGetLastError(hcsErrorInfo* out_info);

#ifdef __cplusplus
}
#endif
