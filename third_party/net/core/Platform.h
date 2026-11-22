#pragma once
#include <string>
#include <chrono>
#include <cstdint>
#if defined(__FreeBSD__)
#include <orbis/libkernel.h>
#endif
namespace dusk::platform { inline std::string resolvePath(const std::string& path) { return path; } }
namespace dusk::platform {
inline uint64_t nowMicros() {
#if defined(__FreeBSD__)
    return sceKernelGetProcessTime();
#else
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
}
