#pragma once

#include <cstdint>

// PS4-only helpers shared by the files in src/platform/ps4.
namespace ns::ps4 {

// Loads and starts a PRX from the system library folder. Tries the sandboxed
// path (/<random word>/common/lib/<name>) first, then /system/common/lib/.
// Returns the module handle (>= 0) or a negative SCE error code.
int32_t loadSystemModule(const char* fileName);

} // namespace ns::ps4
