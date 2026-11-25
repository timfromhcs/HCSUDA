#pragma once

/* Minimal self-registering test harness. No external dependency, no network. */

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace hcstest {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

extern int g_failures;
extern const char* g_currentTest;

void reportFailure(const char* file, int line, const char* expr, const std::string& detail);

inline void reportSkip(const char* reason)
{
    std::printf("SKIP %s: %s\n", g_currentTest, reason);
}

} // namespace hcstest

#define HCS_TEST(name)                                                  \
    static void hcs_test_##name();                                      \
    static ::hcstest::Registrar hcs_registrar_##name(#name, &hcs_test_##name); \
    static void hcs_test_##name()

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) {                                                       \
            ::hcstest::reportFailure(__FILE__, __LINE__, #expr, "");         \
            return;                                                          \
        }                                                                    \
    } while (0)

#define CHECK_MSG(expr, msg)                                                 \
    do {                                                                     \
        if (!(expr)) {                                                       \
            ::hcstest::reportFailure(__FILE__, __LINE__, #expr, (msg));      \
            return;                                                          \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        const auto hcs_a = (a);                                              \
        const auto hcs_b = (b);                                              \
        if (!(hcs_a == hcs_b)) {                                             \
            char hcs_buf[256];                                               \
            std::snprintf(hcs_buf, sizeof(hcs_buf), "expected %s == %s",     \
                          #a, #b);                                           \
            ::hcstest::reportFailure(__FILE__, __LINE__, #a " == " #b,       \
                                     hcs_buf);                               \
            return;                                                          \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        const double hcs_a = static_cast<double>(a);                         \
        const double hcs_b = static_cast<double>(b);                         \
        const double hcs_t = (tol);                                          \
        if (!(std::fabs(hcs_a - hcs_b) <= hcs_t)) {                          \
            char hcs_buf[256];                                               \
            std::snprintf(hcs_buf, sizeof(hcs_buf),                          \
                          "values differ: %.9g vs %.9g (tol %.3g)",          \
                          hcs_a, hcs_b, hcs_t);                              \
            ::hcstest::reportFailure(__FILE__, __LINE__, #a " ~= " #b,       \
                                     hcs_buf);                               \
            return;                                                          \
        }                                                                    \
    } while (0)

#define HCS_REQUIRE_OK(call)                                                 \
    do {                                                                     \
        const hcsResult hcs_r = (call);                                      \
        if (hcs_r != HCS_SUCCESS) {                                          \
            char hcs_buf[512];                                               \
            hcsErrorInfo hcs_info{};                                         \
            hcs_info.struct_size = sizeof(hcs_info);                         \
            hcsGetLastError(&hcs_info);                                      \
            std::snprintf(hcs_buf, sizeof(hcs_buf), "%s failed: %s (%s)",    \
                          #call, hcsResultString(hcs_r), hcs_info.message);  \
            ::hcstest::reportFailure(__FILE__, __LINE__, #call, hcs_buf);    \
            return;                                                          \
        }                                                                    \
    } while (0)
