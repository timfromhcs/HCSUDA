#pragma once

#include "hcsuda/hcsuda.h"

#include <cstddef>
#include <string>

namespace hcs::cudatranslate {

constexpr uint32_t kMaxKernelBytes = 64u << 10;

/*
 * Deliberately narrow source translator. It accepts exactly one CUDA C++ vecadd-style
 * __global__ function shape (documented in docs/compatibility/cuda-api-matrix.md) and emits
 * HCSIR v1. Anything else fails closed with HCS_E_TRANSLATION_UNSUPPORTED; no "best effort"
 * code is emitted. This is not a C++ compiler and not a general CUDA translator.
 */
hcsResult translate(const char* source, size_t size, std::string* outHcsir,
                    std::string* outReportJson);

} // namespace hcs::cudatranslate
