#pragma once

// OpenGL ES 2.0 + EGL 1.4 through Piglet (libScePigletv2VSH). Its header must
// be the only way the GL symbols are pulled in: it defines the platform macros
// before including the Khronos headers.
#include <stdbool.h>
#include <stdint.h>
#include <orbis/Pigletv2VSH.h>

namespace ns::gl {

// Logs (and clears) every pending GL error; true when one was pending.
bool checkError(const char* where);

} // namespace ns::gl
