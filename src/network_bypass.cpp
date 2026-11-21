#include "network_bypass.h"

// HTTP and HTTPS over the console's own sockets, for every download of the
// application. libSceHttp is not used: in a title of category gde a request
// it gives up on (a connect or receive timeout) ends the application, and
// slow servers - GameTorrent answers after 8 to 12 s - or hosts a network
// blocks make those timeouts routine. Here a timeout only closes a socket.
//
// DNS follows src/torrent.c: the system resolver first, then plain UDP
// queries to public servers, rejecting the addresses a blocking resolver
// hands back. TLS is BearSSL 1.2 with the embedded Mozilla roots; the host
// name is kept for SNI, the certificate check and the Host header.

#include "network_ps4.h"

#include "core/Platform.h"
#include "net/Socket.h"
#include "net/TrustStore.h"
#include "net/Url.h"

#include <bearssl.h>

#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include <orbis/Net.h>
#include "../build/sony_ca_bundle.h"

namespace {

constexpr size_t kHeaderLimit = 16384;
constexpr int kConnectTimeoutMs = 8000;
constexpr int kIoIdleMs = 25000;                // GameTorrent answers after 8-12 s
constexpr uint64_t kDeadlineMicros = 90000000;  // one request, connection to last byte

// ---- DNS: system resolver first, public servers as the fallback ------------

struct Addresses {
    char host[64];
    uint32_t ip[4];
    int count = 0;    // public addresses found
    bool tried = false;
    uint64_t until = 0; // process microseconds; 0 = not cached
};

pthread_mutex_t dnsMutex = PTHREAD_MUTEX_INITIALIZER;
Addresses cache[32];
int cacheNext = 0;
int netPool = -1;
int resolver = -1;

bool resolverInit()
{
    static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&initMutex);
    if (resolver >= 0) {
        pthread_mutex_unlock(&initMutex);
        return true;
    }
    netPool = sceNetPoolCreate("NULL_store DNS", 64 * 1024, 0);
    if (netPool < 0) {
        pthread_mutex_unlock(&initMutex);
        return false;
    }
    resolver = sceNetResolverCreate("NULL_store DNS", netPool, 0);
    if (resolver < 0) {
        sceNetPoolDestroy(netPool);
        netPool = -1;
    }
    pthread_mutex_unlock(&initMutex);
    return resolver >= 0;
}

// True for a real public IPv4 address; the ones a filtering resolver is known
// to hand back (0.0.0.0, loopback, RFC 1918, link-local, CGNAT) are rejected.
bool publicIp(uint32_t address)
{
    const auto* b = reinterpret_cast<const uint8_t*>(&address);
    return b[0] != 0 && b[0] != 10 && b[0] != 127 && b[0] < 224 &&
        !(b[0] == 169 && b[1] == 254) && !(b[0] == 172 && b[1] >= 16 && b[1] <= 31) &&
        !(b[0] == 192 && b[1] == 168) && !(b[0] == 100 && b[1] >= 64 && b[1] <= 127);
}

bool publicIpv4Text(const char* text, uint32_t* out)
{
    struct in_addr address;
    return inet_pton(AF_INET, text, &address) == 1 && publicIp(address.s_addr) &&
        (*out = address.s_addr, true);
}

// One plain UDP A query (torrent.c dns_raw). Returns the first public answer.
bool dnsRaw(uint32_t server, const char* host, uint32_t* ip)
{
    uint8_t query[300];
    uint16_t id = static_cast<uint16_t>(dusk::platform::nowMicros() ^ (dusk::platform::nowMicros() >> 16));
    size_t n = 12;
    memset(query, 0, 12);
    query[0] = id >> 8; query[1] = id; query[2] = 1; query[5] = 1;
    for (const char* p = host; *p;) {
        const char* dot = strchr(p, '.');
        const size_t length = dot ? static_cast<size_t>(dot - p) : strlen(p);
        if (!length || length > 63 || n + length + 6 > sizeof query) {
            return false;
        }
        query[n++] = static_cast<uint8_t>(length);
        memcpy(query + n, p, length);
        n += length;
        p += length;
        if (*p == '.') {
            p++;
        }
    }
    query[n++] = 0; query[n++] = 0; query[n++] = 1; query[n++] = 0; query[n++] = 1;
    dusk::net::AbortCheck never = []() { return false; };
    std::vector<uint8_t> reply;
    std::string error;
    if (!dusk::net::udpExchange(server, 53, std::vector<uint8_t>(query, query + n), 1500, never, reply, error)) {
        return false;
    }
    const auto skip = [&](size_t o) {
        for (int hop = 0; hop < 128 && o < reply.size(); hop++) {
            const uint8_t length = reply[o++];
            if (!length) {
                return o;
            }
            if ((length & 0xC0) == 0xC0) {
                return o + 1;
            }
            if (length & 0xC0) {
                return size_t(0);
            }
            o += length;
        }
        return size_t(0);
    };
    if (reply.size() < 12 || (reply[0] << 8 | reply[1]) != id || !(reply[2] & 0x80) || (reply[3] & 0x0F)) {
        return false;
    }
    const unsigned qd = reply[4] << 8 | reply[5];
    const unsigned an = reply[6] << 8 | reply[7];
    size_t o = 12;
    for (unsigned i = 0; i < qd; i++) {
        o = skip(o);
        if (!o || o + 4 > reply.size()) {
            return false;
        }
        o += 4;
    }
    for (unsigned i = 0; i < an && o && o + 10 <= reply.size(); i++) {
        o = skip(o);
        if (!o || o + 10 > reply.size()) {
            break;
        }
        const unsigned type = reply[o] << 8 | reply[o + 1];
        const unsigned cls = reply[o + 2] << 8 | reply[o + 3];
        const unsigned length = reply[o + 8] << 8 | reply[o + 9];
        o += 10;
        if (o + length > reply.size()) {
            break;
        }
        if (type == 1 && cls == 1 && length == 4) {
            uint32_t address = 0;
            memcpy(&address, reply.data() + o, 4);
            if (publicIp(address)) {
                *ip = address;
                return true;
            }
        }
        o += length;
    }
    return false;
}

// Resolves `host` into up to `limit` public addresses. `bypass` says whether
// the public fallback servers may be asked when the system resolver fails.
int resolveHost(const char* host, uint32_t* out, int limit, bool bypass)
{
    if (strlen(host) >= sizeof cache[0].host) {
        return 0;
    }
    uint64_t now = dusk::platform::nowMicros();
    pthread_mutex_lock(&dnsMutex);
    for (int i = 0; i < 32; i++) {
        if (cache[i].host[0] && !strcmp(cache[i].host, host) && cache[i].until > now) {
            const int count = cache[i].count < limit ? cache[i].count : limit;
            for (int k = 0; k < count; k++) {
                out[k] = cache[i].ip[k];
            }
            pthread_mutex_unlock(&dnsMutex);
            return cache[i].tried || count > 0 ? count : -1;
        }
    }
    pthread_mutex_unlock(&dnsMutex);

    int found = 0;
    // One resolver serves every thread: its requests go one at a time.
    static pthread_mutex_t resolveMutex = PTHREAD_MUTEX_INITIALIZER;
    if (resolverInit()) {
        pthread_mutex_lock(&resolveMutex);
        OrbisNetInAddr address;
        int result = sceNetResolverStartNtoa(resolver, host, &address, 0, 0, 0);
        if (result < 0) {
            result = sceNetResolverStartNtoa(resolver, host, &address, 5, 2, 0);
        }
        if (result >= 0 && publicIp(address.s_addr) && found < limit) {
            out[found++] = address.s_addr;
        }
        pthread_mutex_unlock(&resolveMutex);
    }
    if (!found && bypass) {
        const char* const servers[] = { "1.1.1.1", "8.8.8.8" };
        for (const char* server : servers) {
            uint32_t address = 0;
            if (publicIpv4Text(server, &address) && dnsRaw(address, host, &out[found]) && ++found >= limit) {
                break;
            }
        }
    }

    const uint64_t ttl = now + (found ? 30 : 60) * 60 * 1000000ULL;
    pthread_mutex_lock(&dnsMutex);
    Addresses& entry = cache[cacheNext++ % 32];
    snprintf(entry.host, sizeof entry.host, "%s", host);
    for (int k = 0; k < found && k < 4; k++) {
        entry.ip[k] = out[k];
    }
    entry.count = found;
    entry.tried = true;
    entry.until = ttl;
    pthread_mutex_unlock(&dnsMutex);
    return found ? found : -1;
}

// ---- TLS ---------------------------------------------------------------------

dusk::net::TrustStore trust;
pthread_mutex_t trustMutex = PTHREAD_MUTEX_INITIALIZER;
bool trustReady = false;

bool trustInit()
{
    pthread_mutex_lock(&trustMutex);
    if (!trustReady) {
        std::string error;
        trustReady = trust.addPem(portalSonyCaBundle, error) > 0;
        if (!trustReady) {
            portalNetworkTrace("HTTPS trust roots failed: %s", error.c_str());
        }
    }
    pthread_mutex_unlock(&trustMutex);
    return trustReady;
}

// Fills 32 bytes of seed from /dev/urandom, topping up with a local PRNG when
// the console has no such device (Duskaryon's media transport reads it too).
void seedEntropy(unsigned char* seed)
{
    size_t done = 0;
    const int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        while (done < 32) {
            const ssize_t n = read(fd, seed + done, 32 - done);
            if (n <= 0) {
                break;
            }
            done += static_cast<size_t>(n);
        }
        close(fd);
    }
    if (done < 32) {
        uint64_t state = dusk::platform::nowMicros() ^ reinterpret_cast<uint64_t>(&fd);
        while (done < 32) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            seed[done++] = static_cast<unsigned char>(state);
        }
    }
}

// ---- One connection: plain TCP or TLS, with a read buffer --------------------

struct Connection {
    dusk::net::TcpSocket socket;
    bool tls = false;
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    std::vector<unsigned char> tlsBuffer;
    uint64_t deadline = 0;
    unsigned char pending[16384];
    size_t pendingAt = 0;
    size_t pendingSize = 0;

    const std::function<bool()>* cancel = nullptr; // the caller gave up (a paused download)

    bool expired() const
    {
        return dusk::platform::nowMicros() >= deadline || (cancel && *cancel && (*cancel)());
    }

    static int tlsRead(void* ctx, unsigned char* data, size_t size)
    {
        auto* self = static_cast<Connection*>(ctx);
        const int n = self->socket.readSome(data, size, kIoIdleMs, [self] { return self->expired(); });
        return n > 0 ? n : -1;
    }
    static int tlsWrite(void* ctx, const unsigned char* data, size_t size)
    {
        auto* self = static_cast<Connection*>(ctx);
        const int n = self->socket.writeSome(data, size, kIoIdleMs, [self] { return self->expired(); });
        return n > 0 ? n : -1;
    }

    // > 0 bytes read, 0 = the server closed the connection, -1 = error.
    int readRaw(unsigned char* data, size_t size)
    {
        if (expired()) {
            return -1;
        }
        if (!tls) {
            return socket.readSome(data, size, kIoIdleMs, [this] { return expired(); });
        }
        const int n = br_sslio_read(&io, data, size);
        if (n > 0) {
            return n;
        }
        // Many servers close without a TLS close_notify: an I/O error after
        // a complete record is how the end of a "read until close" body looks.
        const int error = br_ssl_engine_last_error(&client.eng);
        return error == BR_ERR_OK || error == BR_ERR_IO ? 0 : -1;
    }

    bool fill()
    {
        if (pendingAt < pendingSize) {
            return true;
        }
        const int n = readRaw(pending, sizeof pending);
        if (n <= 0) {
            return false;
        }
        pendingAt = 0;
        pendingSize = static_cast<size_t>(n);
        return true;
    }

    // One byte; false at the end of the stream or on error.
    bool get(char& c)
    {
        if (!fill()) {
            return false;
        }
        c = static_cast<char>(pending[pendingAt++]);
        return true;
    }

    // Up to `size` bytes: > 0 read, 0 = closed, -1 = error.
    int read(unsigned char* data, size_t size)
    {
        if (pendingAt < pendingSize) {
            const size_t n = std::min(size, pendingSize - pendingAt);
            memcpy(data, pending + pendingAt, n);
            pendingAt += n;
            return static_cast<int>(n);
        }
        return readRaw(data, size);
    }

    bool writeAll(const std::string& text)
    {
        if (tls) {
            return br_sslio_write_all(&io, text.data(), text.size()) == 0 && br_sslio_flush(&io) == 0;
        }
        size_t done = 0;
        while (done < text.size()) {
            const int n = socket.writeSome(reinterpret_cast<const uint8_t*>(text.data()) + done, text.size() - done,
                                           kIoIdleMs, [this] { return expired(); });
            if (n <= 0) {
                return false;
            }
            done += static_cast<size_t>(n);
        }
        return true;
    }
};

// ---- HTTP --------------------------------------------------------------------

// One line ending in CRLF, without it; false on error or an over-long line.
bool readLine(Connection& c, std::string& line, size_t limit)
{
    line.clear();
    char ch = 0;
    while (line.size() < limit) {
        if (!c.get(ch)) {
            return false;
        }
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        line.push_back(ch);
    }
    return false;
}

struct Head {
    int status = 0;
    std::map<std::string, std::string> headers; // lower-case names; the first of repeated ones
};

bool readHead(Connection& c, Head& head, std::string& why)
{
    std::string line;
    size_t total = 0;
    if (!readLine(c, line, kHeaderLimit)) {
        why = c.expired() ? "header_timeout" : "header_read";
        return false;
    }
    if (line.compare(0, 5, "HTTP/") != 0 || line.size() < 12) {
        why = "header_status_line";
        return false;
    }
    head.status = atoi(line.c_str() + 9);
    if (head.status < 100 || head.status > 999) {
        why = "header_status";
        return false;
    }
    for (;;) {
        if (!readLine(c, line, kHeaderLimit)) {
            why = "header_read";
            return false;
        }
        total += line.size();
        if (total > kHeaderLimit) {
            why = "header_limit";
            return false;
        }
        if (line.empty()) {
            return true;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue; // tolerate a malformed line
        }
        std::string key = line.substr(0, colon);
        for (char& k : key) {
            k = static_cast<char>(tolower(static_cast<unsigned char>(k)));
        }
        std::string value = line.substr(colon + 1);
        const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of(" \t");
        value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
        const auto existing = head.headers.find(key);
        if (existing == head.headers.end()) {
            head.headers.emplace(key, value);
        } else if (key == "content-length" && existing->second != value) {
            why = "conflicting_length"; // two lengths: where the body ends is ambiguous
            return false;
        }
    }
}

// Where a body goes: false stops reading (the sink says why through `why`).
using Sink = std::function<bool(const unsigned char*, size_t)>;

// `count` bytes of the body, in pieces, to the sink.
bool pass(Connection& c, uint64_t count, const Sink& sink, std::string& why)
{
    unsigned char buffer[16384];
    while (count > 0) {
        const size_t want = static_cast<size_t>(std::min<uint64_t>(count, sizeof buffer));
        const int n = c.read(buffer, want);
        if (n <= 0) {
            why = c.expired() ? "body_timeout" : "body_read";
            return false;
        }
        if (!sink(buffer, static_cast<size_t>(n))) {
            if (why.empty()) why = "sink";
            return false;
        }
        count -= static_cast<uint64_t>(n);
    }
    return true;
}

bool readBody(Connection& c, const Head& head, const Sink& sink, std::string& why)
{
    const auto encoding = head.headers.find("content-encoding");
    if (encoding != head.headers.end() && encoding->second != "identity") {
        why = "content_encoding";
        return false;
    }
    const auto transfer = head.headers.find("transfer-encoding");
    if (transfer != head.headers.end() && transfer->second.find("chunked") != std::string::npos) {
        std::string line;
        for (;;) {
            if (!readLine(c, line, 1024)) {
                why = "chunk_size";
                return false;
            }
            char* end = nullptr;
            const unsigned long long size = strtoull(line.c_str(), &end, 16);
            if (end == line.c_str()) {
                why = "chunk_size";
                return false;
            }
            if (size == 0) {
                // Trailers, if any, up to the empty line.
                while (readLine(c, line, kHeaderLimit) && !line.empty()) {
                }
                return true;
            }
            if (!pass(c, size, sink, why)) {
                return false;
            }
            if (!readLine(c, line, 16) || !line.empty()) {
                why = "chunk_end";
                return false;
            }
        }
    }
    const auto length = head.headers.find("content-length");
    if (length != head.headers.end()) {
        char* end = nullptr;
        const long long size = strtoll(length->second.c_str(), &end, 10);
        if (end == length->second.c_str() || size < 0) {
            why = "content_length";
            return false;
        }
        return pass(c, static_cast<uint64_t>(size), sink, why);
    }
    // Neither: the body ends when the server closes the connection.
    unsigned char buffer[16384];
    for (;;) {
        const int n = c.read(buffer, sizeof buffer);
        if (n == 0) {
            return true;
        }
        if (n < 0) {
            why = c.expired() ? "body_timeout" : "body_read";
            return false;
        }
        if (!sink(buffer, static_cast<size_t>(n))) {
            if (why.empty()) why = "sink";
            return false;
        }
    }
}

// What one request sends, and how long it may take.
struct Exchange {
    std::string method = "GET";
    const std::vector<std::string>* headers = nullptr;
    const std::string* body = nullptr;
    std::string referer;
    bool range = false;     // ask for the bytes from rangeFrom on (up to rangeEnd, exclusive, when set)
    uint64_t rangeFrom = 0;
    uint64_t rangeEnd = 0;
    bool longTransfer = false; // no overall deadline: only an idle connection ends it
    const std::function<bool()>* cancel = nullptr;
};

// One request. On a redirect fills `location` and returns false. `onHead`
// decides whether the body is wanted (and may refuse the status).
bool exchangeOnce(const dusk::net::Url& url, const Exchange& ex, const std::function<bool(const Head&)>& onHead,
                  const Sink& sink, int& status, std::string& why, std::string& location)
{
    uint32_t addresses[4];
    int count = 0;
    if (dusk::net::parseIpv4(url.host, addresses[0])) {
        count = 1;
    } else {
        count = resolveHost(url.host.c_str(), addresses, 4, true);
    }
    if (count <= 0) {
        why = "dns_unavailable";
        return false;
    }
    const bool secure = url.scheme == "https";
    if (secure && !trustInit()) {
        why = "trust";
        return false;
    }

    auto connection = std::make_unique<Connection>(); // large: keep it off the worker's stack
    Connection& c = *connection;
    c.deadline = ex.longTransfer ? UINT64_MAX : dusk::platform::nowMicros() + kDeadlineMicros;
    c.cancel = ex.cancel;
    bool connected = false;
    std::string lastError;
    for (int i = 0; i < count && !connected; i++) {
        connected = c.socket.connect(addresses[i], url.port, kConnectTimeoutMs, [&c] { return c.expired(); }, lastError);
    }
    if (!connected) {
        why = "connect_failed " + lastError;
        return false;
    }

    if (secure) {
        const time_t now = time(nullptr);
        if (now < 0) {
            why = "clock";
            return false;
        }
        c.tls = true;
        c.tlsBuffer.resize(BR_SSL_BUFSIZE_BIDI);
        br_ssl_client_init_full(&c.client, &c.x509, trust.anchors(), trust.count());
        br_ssl_engine_set_versions(&c.client.eng, BR_TLS12, BR_TLS12);
        br_x509_minimal_set_time(&c.x509, static_cast<uint32_t>(now / 86400) + 719528,
                                 static_cast<uint32_t>(now % 86400));
        br_ssl_engine_set_buffer(&c.client.eng, c.tlsBuffer.data(), c.tlsBuffer.size(), 1);
        unsigned char seed[32];
        seedEntropy(seed);
        br_ssl_engine_inject_entropy(&c.client.eng, seed, sizeof seed);
        if (!br_ssl_client_reset(&c.client, url.host.c_str(), 0)) {
            why = "tls_reset";
            return false;
        }
        br_sslio_init(&c.io, &c.client.eng, Connection::tlsRead, &c, Connection::tlsWrite, &c);
    }

    const bool defaultPort = (secure && url.port == 443) || (!secure && url.port == 80);
    std::string request = ex.method + " " + url.target + " HTTP/1.1\r\nHost: " + url.host +
                          (defaultPort ? std::string() : ":" + std::to_string(url.port)) +
                          "\r\nUser-Agent: NULL_store/1.0\r\nAccept: */*\r\nAccept-Encoding: identity\r\n";
    if (!ex.referer.empty()) {
        request += "Referer: " + ex.referer + "\r\n";
    }
    if (ex.headers) {
        for (const std::string& header : *ex.headers) {
            request += header + "\r\n";
        }
    }
    if (ex.range) {
        request += "Range: bytes=" + std::to_string(ex.rangeFrom) + "-" +
                   (ex.rangeEnd > ex.rangeFrom ? std::to_string(ex.rangeEnd - 1) : std::string()) + "\r\n";
    }
    if (ex.body || ex.method == "POST" || ex.method == "PUT") {
        request += "Content-Length: " + std::to_string(ex.body ? ex.body->size() : 0) + "\r\n";
    }
    request += "Connection: close\r\n\r\n";
    if (!c.writeAll(request) || (ex.body && !ex.body->empty() && !c.writeAll(*ex.body))) {
        const int tlsError = secure ? br_ssl_engine_last_error(&c.client.eng) : 0;
        why = tlsError ? "tls_error " + std::to_string(tlsError) : "send_request";
        return false;
    }

    Head head;
    if (!readHead(c, head, why)) {
        if (secure && br_ssl_engine_last_error(&c.client.eng) > 0 && br_ssl_engine_last_error(&c.client.eng) != BR_ERR_IO) {
            why = "tls_error " + std::to_string(br_ssl_engine_last_error(&c.client.eng));
        }
        return false;
    }
    status = head.status;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const auto found = head.headers.find("location");
        if (found == head.headers.end() || found->second.empty()) {
            why = "redirect_without_location";
            return false;
        }
        location = found->second;
        return false;
    }
    if (!onHead(head)) {
        if (why.empty()) why = "http_status";
        return false;
    }
    return readBody(c, head, sink, why);
}

// Follows redirects; a 301, 302 or 303 turns a POST into a GET.
bool exchange(const std::string& url, Exchange ex, const std::function<bool(const Head&)>& onHead, const Sink& sink,
              const std::function<void()>& restart, int& status)
{
    std::string current = url;
    for (int hop = 0; hop < 6; hop++) {
        dusk::net::Url parsed;
        if (!dusk::net::parseUrl(current, parsed) || (parsed.scheme != "https" && parsed.scheme != "http")) {
            portalNetworkTrace("%s failed url=%.120s hop=%d reason=invalid_url", ex.method.c_str(), current.c_str(), hop);
            return false;
        }
        std::string why, location;
        status = 0;
        if (exchangeOnce(parsed, ex, onHead, sink, status, why, location)) {
            portalNetworkTrace("%s ok host=%s hop=%d status=%d", ex.method.c_str(), parsed.host.c_str(), hop, status);
            return true;
        }
        restart();
        if (location.empty()) {
            portalNetworkTrace("%s failed host=%s hop=%d reason=%s status=%d", ex.method.c_str(), parsed.host.c_str(),
                               hop, why.c_str(), status);
            return false;
        }
        const std::string next = dusk::net::resolveLocation(parsed, location);
        if (next.empty() || next == current) {
            portalNetworkTrace("%s failed host=%s hop=%d reason=redirect_invalid", ex.method.c_str(),
                               parsed.host.c_str(), hop);
            return false;
        }
        if (status != 307 && status != 308 && ex.method != "GET") {
            ex.method = "GET";
            ex.body = nullptr;
        }
        current = next;
    }
    portalNetworkTrace("%s failed url=%.120s reason=redirect_limit", ex.method.c_str(), current.c_str());
    return false;
}

// A sink that keeps at most `cap` bytes in `out`.
Sink keep(std::string& out, size_t cap)
{
    return [&out, cap](const unsigned char* data, size_t size) {
        if (out.size() + size > cap) {
            return false;
        }
        out.append(reinterpret_cast<const char*>(data), size);
        return true;
    };
}

} // namespace

bool portalSocketFetch(const std::string& url, std::string& out, size_t cap, const std::string& referer)
{
    out.clear();
    Exchange ex;
    ex.referer = referer;
    int status = 0;
    return exchange(url, ex, [](const Head& head) { return head.status == 200; }, keep(out, cap),
                    [&out] { out.clear(); }, status);
}

bool portalSocketRequest(const std::string& url, const PortalRequest& request, std::string& out, size_t cap,
                         int& status)
{
    out.clear();
    Exchange ex;
    ex.method = request.method;
    ex.headers = &request.headers;
    ex.body = request.method == "GET" ? nullptr : &request.body;
    ex.referer = request.referer;
    return exchange(url, ex, [](const Head&) { return true; }, keep(out, cap), [&out] { out.clear(); }, status);
}

bool portalSocketDownload(const std::string& url, PortalStream& stream, std::string& why)
{
    Exchange ex;
    ex.range = stream.offset > 0 || stream.end > 0;
    ex.rangeFrom = stream.offset;
    ex.rangeEnd = stream.end;
    ex.longTransfer = true;
    ex.cancel = &stream.stopped;
    int status = 0;
    bool refused = false;
    const auto onHead = [&](const Head& head) {
        uint64_t start = 0, total = 0;
        const auto length = head.headers.find("content-length");
        const uint64_t body = length == head.headers.end() ? 0 : strtoull(length->second.c_str(), nullptr, 10);
        if (head.status == 206) {
            // "bytes 100-199/200"
            const auto range = head.headers.find("content-range");
            if (range == head.headers.end()) {
                refused = true;
                return false;
            }
            const char* text = range->second.c_str();
            const char* bytes = strstr(text, "bytes ");
            start = strtoull(bytes ? bytes + 6 : text, nullptr, 10);
            const char* slash = strchr(text, '/');
            total = slash && slash[1] != '*' ? strtoull(slash + 1, nullptr, 10) : 0;
        } else if (head.status == 200) {
            total = body; // the whole file, from its first byte
        } else if (head.status == 416 && stream.offset > 0) {
            // Nothing after the offset: the file is complete.
            refused = !stream.begin(stream.offset, stream.offset);
            return false;
        } else {
            refused = true;
            return false;
        }
        if (!stream.begin(start, total)) {
            refused = true;
            return false;
        }
        return true;
    };
    const bool ok = exchange(url, ex, onHead, stream.write, [] {}, status);
    why = ok ? "" : status == 416 && !refused ? "complete" : "http " + std::to_string(status);
    return ok || (status == 416 && !refused);
}
