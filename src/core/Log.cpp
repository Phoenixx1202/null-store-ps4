#include "core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

#if defined(NS_PLATFORM_PS4)
#include <orbis/libkernel.h>
#include <pthread.h>
#endif

namespace ns::log {

namespace {

std::string g_path;
constexpr long kMaxLogBytes = 512 * 1024;

#if defined(NS_PLATFORM_PS4)
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

} // namespace

void setDirectory(const char* directory)
{
    g_path = std::string(directory) + "/app.log";
    // Start a new file once the previous sessions have filled it.
    if (FILE* existing = fopen(g_path.c_str(), "rb")) {
        fseek(existing, 0, SEEK_END);
        const long size = ftell(existing);
        fclose(existing);
        if (size > kMaxLogBytes) {
            remove(g_path.c_str());
        }
    }
}

void write(const char* level, const char* format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    char line[1100];
    snprintf(line, sizeof(line), "[NULL_store][%s] %s\n", level, text);

#if defined(NS_PLATFORM_PS4)
    // The export takes already formatted text, not a printf format string.
    sceKernelDebugOutText(0, line);
    pthread_mutex_lock(&g_mutex);
    if (!g_path.empty()) {
        if (FILE* file = fopen(g_path.c_str(), "ab")) {
            fprintf(file, "[%lld]%s", static_cast<long long>(time(nullptr)), line);
            fclose(file);
        }
    }
    pthread_mutex_unlock(&g_mutex);
#else
    fputs(line, stderr);
#endif
}

} // namespace ns::log
