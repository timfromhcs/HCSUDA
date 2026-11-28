/*
 * HCSPORT — CUDA source compatibility scanner and narrow translator (V1).
 *
 * V1 performs a conservative compatibility scan and translates exactly one documented
 * vecadd kernel shape via `--convert`. It is not a CUDA compiler: every other construct
 * fails closed with a machine-readable report, and unsupported CUDA libraries, PTX, CUBIN
 * and SASS remain explicitly unsupported.
 */

#include "translation/cuda_source/scanner.h"
#include "translation/cuda_source/translator.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool readFile(const char* path, std::string* out)
{
    std::FILE* file = std::fopen(path, "rb");
    if (!file) {
        return false;
    }
    char chunk[8192];
    size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        out->append(chunk, read);
        if (out->size() > hcs::cudascan::kMaxSourceBytes + 1) {
            std::fclose(file);
            return false;
        }
    }
    std::fclose(file);
    return true;
}

void printError(const char* operation, hcsResult result)
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    std::fprintf(stderr, "%s failed: %s (%s)\n", operation, hcsResultString(result),
                 info.message);
}

} // namespace

int main(int argc, char** argv)
{
    const char* input = nullptr;
    bool json = false;
    bool convert = false;
    const char* output = nullptr;
    const char* report = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: hcsport --scan <file.cu> [--json]\n"
                        "       hcsport --convert <file.cu> [--output out.hcsir] "
                        "[--report report.json]\n");
            return 0;
        } else if (std::strcmp(argv[i], "--scan") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (std::strcmp(argv[i], "--convert") == 0 && i + 1 < argc) {
            convert = true;
            input = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            report = argv[++i];
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json = true;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 2;
        }
    }

    if (!input) {
        std::fprintf(stderr, "no input file; use --scan <file.cu>\n");
        return 2;
    }
    const bool doConvert = convert;

    std::string source;
    if (!readFile(input, &source)) {
        std::fprintf(stderr, "cannot read source (or file exceeds 4 MiB): %s\n", input);
        return 1;
    }

    if (doConvert) {
        std::string hcsir;
        std::string reportJson;
        const hcsResult conversion =
            hcs::cudatranslate::translate(source.data(), source.size(), &hcsir, &reportJson);
        if (conversion != HCS_SUCCESS) {
            printError("hcsport convert", conversion);
            return 1;
        }
        if (output) {
            std::FILE* file = std::fopen(output, "wb");
            if (!file) {
                std::fprintf(stderr, "cannot write output: %s\n", output);
                return 1;
            }
            std::fwrite(hcsir.data(), 1, hcsir.size(), file);
            std::fclose(file);
        } else {
            std::fwrite(hcsir.data(), 1, hcsir.size(), stdout);
        }
        if (report) {
            std::FILE* file = std::fopen(report, "wb");
            if (!file) {
                std::fprintf(stderr, "cannot write report: %s\n", report);
                return 1;
            }
            std::fwrite(reportJson.data(), 1, reportJson.size(), file);
            std::fclose(file);
        }
        std::fprintf(stderr,
                     "hcsport: translated the supported vecadd subset; host code still "
                     "requires manual review.\n");
        return 0;
    }

    hcs::cudascan::Report scanReport;
    const hcsResult result = hcs::cudascan::scan(source.data(), source.size(), &scanReport);
    if (result != HCS_SUCCESS) {
        printError("hcsport scan", result);
        return 1;
    }

    if (json) {
        std::printf("%s\n", hcs::cudascan::toJson(scanReport).c_str());
        return 0;
    }

    std::printf("HCSPORT compatibility scan: %s\n", input);
    std::printf("  bytes %llu, lines %u\n", static_cast<unsigned long long>(scanReport.bytes),
                scanReport.lines);
    std::printf("  manual_port_required %u, unsupported %u, translated 0\n",
                scanReport.manualCount, scanReport.unsupportedCount);
    std::printf("  V1 has no automatic translator; no source is rewritten.\n\n");
    for (const auto& finding : scanReport.findings) {
        std::printf("  %s:%u:%u [%s] %s — %s\n", input, finding.line, finding.column,
                    hcs::cudascan::classificationName(finding.classification),
                    finding.construct.c_str(), finding.detail.c_str());
    }
    return 0;
}
