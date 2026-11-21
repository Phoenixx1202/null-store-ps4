#pragma once

// Application log. On the PS4 every line goes to the kernel log (klog, visible
// over GoldHEN's port 3232) and is appended to <data>/app.log; the host
// preview prints to stderr.
namespace ns::log {

// Sets the folder that receives app.log. Until then lines only reach klog.
void setDirectory(const char* directory);

void write(const char* level, const char* format, ...) __attribute__((format(printf, 2, 3)));

} // namespace ns::log

#define NS_LOG_INFO(...) ::ns::log::write("INFO", __VA_ARGS__)
#define NS_LOG_WARN(...) ::ns::log::write("WARN", __VA_ARGS__)
#define NS_LOG_ERROR(...) ::ns::log::write("ERROR", __VA_ARGS__)
