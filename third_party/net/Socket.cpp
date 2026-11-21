#include "net/Socket.h"
#include "net/SocketErrors.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/Platform.h"

namespace dusk::net {

namespace {

constexpr int kSliceMs = 200;

// SIGPIPE is ignored process-wide (net::init); Linux can also be told per call.
#if defined(__linux__)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

std::string errnoText(const char* what, int err)
{
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s failed (errno %d)", what, err);
    return buffer;
}

bool setNonBlocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

sockaddr_in makeAddress(uint32_t addressBe, uint16_t port)
{
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
#if defined(__FreeBSD__)
    address.sin_len = sizeof(address);
#endif
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = addressBe;
    return address;
}

// 1 = ready, 0 = timed out, -1 = aborted or poll error (`error` set).
int waitReady(int fd, short events, int timeoutMs, const AbortCheck& aborted, std::string& error)
{
    const uint64_t deadline = platform::nowMicros() + static_cast<uint64_t>(timeoutMs) * 1000;
    for (;;) {
        if (aborted && aborted()) {
            error = "aborted";
            return -1;
        }
        const uint64_t now = platform::nowMicros();
        if (now >= deadline) {
            return 0;
        }
        const uint64_t leftMs = (deadline - now + 999) / 1000;
        pollfd entry;
        entry.fd = fd;
        entry.events = events;
        entry.revents = 0;
        const int rc = poll(&entry, 1, leftMs < kSliceMs ? static_cast<int>(leftMs) : kSliceMs);
        if (rc > 0) {
            return 1; // also on POLLERR/POLLHUP: the next call reports the error
        }
        if (rc < 0 && errno != socketerrors::interrupted) {
            error = errnoText("poll", errno);
            return -1;
        }
    }
}

} // namespace

std::string ipv4Text(uint32_t addressBe)
{
    uint8_t bytes[4];
    memcpy(bytes, &addressBe, sizeof(bytes));
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return buffer;
}

TcpSocket::~TcpSocket()
{
    close();
}

void TcpSocket::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool TcpSocket::connect(uint32_t addressBe, uint16_t port, int timeoutMs, const AbortCheck& aborted,
                        std::string& error)
{
    close();
    const std::string target = ipv4Text(addressBe) + ":" + std::to_string(port);
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        error = errnoText("socket", errno);
        return false;
    }
#ifdef SO_NOSIGPIPE
    int suppressSignal = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &suppressSignal, sizeof(suppressSignal)) != 0) { close(); return false; }
#endif
    if (!setNonBlocking(fd_)) {
        error = errnoText("fcntl(O_NONBLOCK)", errno);
        close();
        return false;
    }
    const sockaddr_in address = makeAddress(addressBe, port);
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
        return true;
    }
    if (!socketerrors::connecting(errno)) {
        error = errnoText(("connect to " + target).c_str(), errno);
        close();
        return false;
    }
    std::string waitError;
    const int ready = waitReady(fd_, POLLOUT, timeoutMs, aborted, waitError);
    if (ready <= 0) {
        error = ready == 0 ? "no connection to " + target + " within " + std::to_string(timeoutMs / 1000) + " s"
                           : waitError;
        close();
        return false;
    }
    int soError = 0;
    socklen_t length = sizeof(soError);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &length) != 0 || soError != 0) {
        error = errnoText(("connect to " + target).c_str(), soError);
        close();
        return false;
    }
    return true;
}

bool TcpSocket::wait(short events, int timeoutMs, const AbortCheck& aborted)
{
    const int ready = waitReady(fd_, events, timeoutMs, aborted, error_);
    if (ready == 0) {
        error_ = "no progress for " + std::to_string(timeoutMs / 1000) + " s";
    }
    return ready > 0;
}

int TcpSocket::readSome(uint8_t* data, size_t size, int idleMs, const AbortCheck& aborted)
{
    if (fd_ < 0) {
        error_ = "not connected";
        return -1;
    }
    const size_t chunk = size < static_cast<size_t>(INT_MAX) ? size : static_cast<size_t>(INT_MAX);
    for (;;) {
        const ssize_t n = recv(fd_, data, chunk, 0);
        if (n >= 0) {
            return static_cast<int>(n);
        }
        if (errno == socketerrors::interrupted) {
            continue;
        }
        if (!socketerrors::wouldBlock(errno)) {
            error_ = errnoText("recv", errno);
            return -1;
        }
        if (!wait(POLLIN, idleMs, aborted)) {
            return -1;
        }
    }
}

int TcpSocket::writeSome(const uint8_t* data, size_t size, int idleMs, const AbortCheck& aborted)
{
    if (fd_ < 0) {
        error_ = "not connected";
        return -1;
    }
    const size_t chunk = size < static_cast<size_t>(INT_MAX) ? size : static_cast<size_t>(INT_MAX);
    for (;;) {
        const ssize_t n = send(fd_, data, chunk, kSendFlags);
        if (n > 0) {
            return static_cast<int>(n);
        }
        if (n < 0 && errno == socketerrors::interrupted) {
            continue;
        }
        if (n < 0 && !socketerrors::wouldBlock(errno)) {
            error_ = errnoText("send", errno);
            return -1;
        }
        if (!wait(POLLOUT, idleMs, aborted)) {
            return -1;
        }
    }
}

bool udpExchange(uint32_t addressBe, uint16_t port, const std::vector<uint8_t>& request, int timeoutMs,
                 const AbortCheck& aborted, std::vector<uint8_t>& reply, std::string& error)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        error = errnoText("socket(udp)", errno);
        return false;
    }
    bool ok = false;
    const sockaddr_in address = makeAddress(addressBe, port);
    if (!setNonBlocking(fd)) {
        error = errnoText("fcntl(O_NONBLOCK)", errno);
    } else if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        error = errnoText("connect(udp)", errno);
    } else if (send(fd, request.data(), request.size(), kSendFlags) != static_cast<ssize_t>(request.size())) {
        error = errnoText("send(udp)", errno);
    } else {
        const uint64_t deadline = platform::nowMicros() + static_cast<uint64_t>(timeoutMs) * 1000;
        for (;;) {
            const uint64_t now = platform::nowMicros();
            const int leftMs = now >= deadline ? 0 : static_cast<int>((deadline - now) / 1000);
            const int ready = waitReady(fd, POLLIN, leftMs, aborted, error);
            if (ready <= 0) {
                if (ready == 0) {
                    error = "no answer from " + ipv4Text(addressBe);
                }
                break;
            }
            reply.resize(1536);
            const ssize_t n = recv(fd, reply.data(), reply.size(), 0);
            if (n > 0) {
                reply.resize(static_cast<size_t>(n));
                ok = true;
                break;
            }
            if (n < 0 && !socketerrors::wouldBlock(errno) && errno != socketerrors::interrupted) {
                error = errnoText("recv(udp)", errno);
                break;
            }
        }
    }
    ::close(fd);
    return ok;
}

} // namespace dusk::net
