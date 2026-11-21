#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Non-blocking sockets with our own timeouts (PS4: the libkernel socket calls,
// not libSceNet). Every wait polls in short slices and gives up as soon as
// `aborted()` turns true.
namespace dusk::net {

using AbortCheck = std::function<bool()>;

class TcpSocket {
public:
    TcpSocket() = default;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    bool connect(uint32_t addressBe, uint16_t port, int timeoutMs, const AbortCheck& aborted, std::string& error);
    // > 0 bytes moved, 0 = orderly close (read only), -1 = error, timeout or
    // abort (see lastError()). Waits at most `idleMs` for the socket to be ready.
    int readSome(uint8_t* data, size_t size, int idleMs, const AbortCheck& aborted);
    int writeSome(const uint8_t* data, size_t size, int idleMs, const AbortCheck& aborted);
    const std::string& lastError() const { return error_; }
    void close();

private:
    bool wait(short events, int timeoutMs, const AbortCheck& aborted);

    int fd_ = -1;
    std::string error_;
};

// One UDP request/response exchange (DNS).
bool udpExchange(uint32_t addressBe, uint16_t port, const std::vector<uint8_t>& request, int timeoutMs,
                 const AbortCheck& aborted, std::vector<uint8_t>& reply, std::string& error);

std::string ipv4Text(uint32_t addressBe);

} // namespace dusk::net
