#include "test_framework.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace hcstest {

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;
const char* g_currentTest = "<none>";

void reportFailure(const char* file, int line, const char* expr, const std::string& detail)
{
    ++g_failures;
    std::printf("FAIL %s\n  %s:%d: %s%s%s\n", g_currentTest, file, line, expr,
                detail.empty() ? "" : " | ", detail.c_str());
}

} // namespace hcstest

int main(int argc, char** argv)
{
    const char* filter = nullptr;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            listOnly = true;
        } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        }
    }

    auto& tests = hcstest::registry();
    if (listOnly) {
        for (const auto& test : tests) {
            std::printf("%s\n", test.name);
        }
        return 0;
    }

    std::printf("HCSUDA unit tests: %zu registered\n", tests.size());
    int run = 0;
    for (const auto& test : tests) {
        if (filter && std::strstr(test.name, filter) == nullptr) {
            continue;
        }
        hcstest::g_currentTest = test.name;
        const int before = hcstest::g_failures;
        std::printf("RUN  %s\n", test.name);
        std::fflush(stdout);
        test.fn();
        ++run;
        if (hcstest::g_failures == before) {
            std::printf("PASS %s\n", test.name);
            std::fflush(stdout);
        }
    }
    std::printf("\n%d tests run, %d failures\n", run, hcstest::g_failures);
    return hcstest::g_failures == 0 ? 0 : 1;
}
