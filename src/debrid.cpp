#include "debrid.h"

#include "catalog.h"
#include "json_lite.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <vector>
#ifdef _WIN32
#include <io.h>
#include <thread>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace {

const char* const kNames[] = { "Off", "Real-Debrid", "AllDebrid", "Premiumize", "TorBox", "Debrid-Link" };
constexpr int kServices = 6;
constexpr const char* kAgent = "NULL_store";
constexpr int kPollMs = 5000;
constexpr int kAttempts = 6; // per file, before the download fails

enum Service { REAL_DEBRID = 1, ALL_DEBRID = 2, PREMIUMIZE = 3, TORBOX = 4, DEBRID_LINK = 5 };

double secondsNow()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string urlEncode(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// ---- The torrent: name, trackers, magnet link ------------------------------

// Skips one bencoded value at `i`; false when malformed.
bool skipValue(const std::string& s, size_t& i, int depth = 0)
{
    if (i >= s.size() || depth > 32) return false;
    const char c = s[i];
    if (c == 'i') {
        const size_t end = s.find('e', i);
        if (end == std::string::npos) return false;
        i = end + 1;
        return true;
    }
    if (c == 'l' || c == 'd') {
        ++i;
        while (i < s.size() && s[i] != 'e') {
            if (!skipValue(s, i, depth + 1)) return false;
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }
    if (isdigit(static_cast<unsigned char>(c))) {
        const size_t colon = s.find(':', i);
        if (colon == std::string::npos) return false;
        const size_t length = strtoul(s.c_str() + i, nullptr, 10);
        if (colon + 1 + length > s.size()) return false;
        i = colon + 1 + length;
        return true;
    }
    return false;
}

bool readString(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || !isdigit(static_cast<unsigned char>(s[i]))) return false;
    const size_t colon = s.find(':', i);
    if (colon == std::string::npos) return false;
    const size_t length = strtoul(s.c_str() + i, nullptr, 10);
    if (colon + 1 + length > s.size()) return false;
    out = s.substr(colon + 1, length);
    i = colon + 1 + length;
    return true;
}

// Every string inside the value at `i` (a list of lists of trackers).
void collectStrings(const std::string& s, size_t& i, std::vector<std::string>& out, int depth = 0)
{
    if (i >= s.size() || depth > 4) return;
    if (s[i] == 'l') {
        ++i;
        while (i < s.size() && s[i] != 'e') {
            const size_t before = i;
            collectStrings(s, i, out, depth + 1);
            if (i == before && !skipValue(s, i)) return;
        }
        ++i;
        return;
    }
    std::string text;
    if (readString(s, i, text)) {
        out.push_back(text);
    } else {
        skipValue(s, i);
    }
}

struct TorrentFacts {
    std::string name;
    std::vector<std::string> trackers;
};

TorrentFacts torrentFacts(const std::string& meta)
{
    TorrentFacts facts;
    if (meta.empty() || meta[0] != 'd') return facts;
    size_t i = 1;
    while (i < meta.size() && meta[i] != 'e') {
        std::string key;
        if (!readString(meta, i, key)) break;
        if (key == "announce") {
            std::string tracker;
            if (readString(meta, i, tracker)) facts.trackers.push_back(tracker);
            else if (!skipValue(meta, i)) break;
        } else if (key == "announce-list") {
            collectStrings(meta, i, facts.trackers);
        } else if (key == "info" && i < meta.size() && meta[i] == 'd') {
            size_t j = i + 1;
            while (j < meta.size() && meta[j] != 'e') {
                std::string field;
                if (!readString(meta, j, field)) break;
                if (field == "name") {
                    readString(meta, j, facts.name);
                } else if (!skipValue(meta, j)) {
                    break;
                }
            }
            if (!skipValue(meta, i)) break;
        } else if (!skipValue(meta, i)) {
            break;
        }
    }
    std::vector<std::string> unique;
    for (const std::string& tracker : facts.trackers) {
        if (tracker.size() < 400 && std::find(unique.begin(), unique.end(), tracker) == unique.end() &&
            unique.size() < 20) {
            unique.push_back(tracker);
        }
    }
    facts.trackers.swap(unique);
    return facts;
}

// A file name of the transfer as it is saved: its last part, without
// characters the console's file system refuses.
std::string safeName(const std::string& path)
{
    std::string name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    for (char& c : name) {
        if (static_cast<unsigned char>(c) < 32 || strchr(":*?\"<>|", c)) c = '_';
    }
    if (name.empty() || name == "." || name == "..") name = "file";
    return name.size() > 200 ? name.substr(name.size() - 200) : name;
}

// ---- One service ------------------------------------------------------------

struct RemoteFile {
    std::string name;
    uint64_t size = 0;
    std::string ref; // what link() turns into an address: a link, an id
};

struct Remote {
    enum State { Waiting, Ready, Failed, Missing } state = Waiting;
    std::string message; // the service's own words, for the queue
    double progress = 0; // 0..1 of the service's own download
    uint64_t size = 0;
    std::vector<RemoteFile> files;
};

class Client {
public:
    Client(const DebridNet& net, const DebridConfig& config) : net_(net), config_(config) {}

    const char* name() const { return kNames[config_.service]; }

    // Hands the torrent to the service; false with `why`.
    bool add(const std::string& meta, std::string& id, std::string& why);
    // Where the transfer is.
    bool status(const std::string& id, Remote& remote, std::string& why);
    // The download address of one file.
    bool link(const std::string& id, const RemoteFile& file, std::string& url, std::string& why);

private:
    const DebridNet& net_;
    const DebridConfig& config_;

    std::vector<std::string> auth() const
    {
        return { "Authorization: Bearer " + config_.key };
    }

    // A request whose answer is JSON. False when nothing usable came back.
    bool call(const std::string& method, const std::string& url, std::vector<std::string> headers,
              const std::string& body, json::Value& answer, int& status, std::string& why) const
    {
        std::string text;
        status = 0;
        if (!net_.request(method, url, headers, body, text, status)) {
            why = std::string("Could not reach ") + name() + ". Check the internet connection.";
            return false;
        }
        answer = json::parse(text);
        if (status == 401 || status == 403) {
            why = std::string(name()) + " refused the API key. Check it in Settings.";
            return false;
        }
        if (answer.isNull() && status != 204) {
            why = std::string(name()) + " sent an answer that could not be read (HTTP " + std::to_string(status) + ").";
            return false;
        }
        return true;
    }

    static std::string multipart(const std::string& boundary, const std::vector<std::pair<std::string, std::string>>& fields,
                                 const std::string& fileField, const std::string& meta)
    {
        std::string body;
        for (const auto& field : fields) {
            body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + field.first + "\"\r\n\r\n" +
                    field.second + "\r\n";
        }
        if (!fileField.empty()) {
            body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + fileField +
                    "\"; filename=\"game.torrent\"\r\nContent-Type: application/x-bittorrent\r\n\r\n" + meta + "\r\n";
        }
        body += "--" + boundary + "--\r\n";
        return body;
    }

    static std::string boundary()
    {
        char text[48];
        snprintf(text, sizeof text, "----NULLstore%08lx%04x", static_cast<unsigned long>(time(nullptr)),
                 static_cast<unsigned>(rand() & 0xFFFF));
        return text;
    }

    std::string serviceError(const json::Value& answer) const
    {
        // Each service names its errors its own way.
        const json::Value& error = answer["error"];
        std::string text = error.isObject() ? error["message"].str() : error.str();
        if (text.empty()) text = answer["detail"].str();
        if (text.empty()) text = answer["message"].str();
        if (text.empty()) text = answer["error_description"].str();
        return std::string(name()) + ": " + (text.empty() ? "the request was refused" : text);
    }

    void flattenAllDebrid(const json::Value& entries, const std::string& folder, std::vector<RemoteFile>& out, int depth) const;
    bool premiumizeFolder(const std::string& folder, std::vector<RemoteFile>& out, std::string& why, int depth) const;
};

bool Client::add(const std::string& meta, std::string& id, std::string& why)
{
    json::Value answer;
    int status = 0;
    switch (config_.service) {
    case REAL_DEBRID: {
        std::vector<std::string> headers = auth();
        headers.push_back("Content-Type: application/x-bittorrent");
        if (!call("PUT", "https://api.real-debrid.com/rest/1.0/torrents/addTorrent", headers, meta, answer, status, why))
            return false;
        id = answer["id"].str();
        break;
    }
    case ALL_DEBRID: {
        const std::string b = boundary();
        std::vector<std::string> headers = auth();
        headers.push_back("Content-Type: multipart/form-data; boundary=" + b);
        if (!call("POST", std::string("https://api.alldebrid.com/v4/magnet/upload/file?agent=") + kAgent, headers,
                  multipart(b, {}, "files[0]", meta), answer, status, why))
            return false;
        const json::Value& file = answer["data"]["files"].at(0);
        if (!file["error"].isNull()) {
            why = std::string(name()) + ": " + file["error"]["message"].str();
            return false;
        }
        id = file["id"].str();
        break;
    }
    case PREMIUMIZE: {
        const std::string b = boundary();
        if (!call("POST", "https://www.premiumize.me/api/transfer/create?apikey=" + urlEncode(config_.key),
                  { "Content-Type: multipart/form-data; boundary=" + b }, multipart(b, {}, "file", meta), answer,
                  status, why))
            return false;
        if (answer["status"].str() != "success") {
            why = serviceError(answer);
            return false;
        }
        id = answer["id"].str();
        break;
    }
    case TORBOX: {
        const std::string b = boundary();
        std::vector<std::string> headers = auth();
        headers.push_back("Content-Type: multipart/form-data; boundary=" + b);
        if (!call("POST", "https://api.torbox.app/v1/api/torrents/createtorrent", headers,
                  multipart(b, { { "allow_zip", "false" } }, "file", meta), answer, status, why))
            return false;
        if (!answer["success"].truthy()) {
            why = serviceError(answer);
            return false;
        }
        id = answer["data"]["torrent_id"].str();
        break;
    }
    case DEBRID_LINK: {
        std::vector<std::string> headers = auth();
        headers.push_back("Content-Type: application/x-www-form-urlencoded");
        if (!call("POST", "https://debrid-link.com/api/v2/seedbox/add", headers,
                  "url=" + urlEncode(debridMagnet(meta)) + "&async=true", answer, status, why))
            return false;
        if (!answer["success"].truthy()) {
            why = serviceError(answer);
            return false;
        }
        id = answer["value"]["id"].str();
        break;
    }
    default:
        why = "No Debrid service is chosen.";
        return false;
    }
    if (id.empty()) {
        why = serviceError(answer);
        return false;
    }
    return true;
}

void Client::flattenAllDebrid(const json::Value& entries, const std::string& folder, std::vector<RemoteFile>& out,
                              int depth) const
{
    if (depth > 8) return;
    for (size_t i = 0; i < entries.size(); i++) {
        const json::Value& entry = entries.at(i);
        const std::string path = folder.empty() ? entry["n"].str() : folder + "/" + entry["n"].str();
        if (entry["e"].isArray()) {
            flattenAllDebrid(entry["e"], path, out, depth + 1);
        } else if (!entry["l"].str().empty()) {
            out.push_back({ path, entry["s"].u64(), entry["l"].str() });
        }
    }
}

bool Client::premiumizeFolder(const std::string& folder, std::vector<RemoteFile>& out, std::string& why, int depth) const
{
    if (depth > 6) return true;
    json::Value answer;
    int status = 0;
    if (!call("GET", "https://www.premiumize.me/api/folder/list?apikey=" + urlEncode(config_.key) + "&id=" + urlEncode(folder),
              {}, "", answer, status, why))
        return false;
    if (answer["status"].str() != "success") {
        why = serviceError(answer);
        return false;
    }
    const json::Value& content = answer["content"];
    for (size_t i = 0; i < content.size(); i++) {
        const json::Value& item = content.at(i);
        if (item["type"].str() == "folder") {
            if (!premiumizeFolder(item["id"].str(), out, why, depth + 1)) return false;
        } else if (!item["link"].str().empty()) {
            out.push_back({ item["name"].str(), item["size"].u64(), item["link"].str() });
        }
    }
    return true;
}

bool Client::status(const std::string& id, Remote& remote, std::string& why)
{
    remote = Remote();
    json::Value answer;
    int status = 0;
    switch (config_.service) {
    case REAL_DEBRID: {
        const std::string base = "https://api.real-debrid.com/rest/1.0/torrents/";
        if (!call("GET", base + "info/" + id, auth(), "", answer, status, why)) return false;
        if (status == 404) {
            remote.state = Remote::Missing;
            return true;
        }
        const std::string state = answer["status"].str();
        remote.message = state;
        remote.progress = answer["progress"].num() / 100.0;
        remote.size = answer["bytes"].u64();
        if (state == "waiting_files_selection") {
            std::vector<std::string> headers = auth();
            headers.push_back("Content-Type: application/x-www-form-urlencoded");
            json::Value ignored;
            if (!call("POST", base + "selectFiles/" + id, headers, "files=all", ignored, status, why)) return false;
            remote.message = "files selected";
        } else if (state == "magnet_error" || state == "error" || state == "virus" || state == "dead") {
            remote.state = Remote::Failed;
            remote.message = std::string(name()) + " could not download this torrent (" + state + ").";
        } else if (state == "downloaded") {
            // The links follow the selected files, in order.
            const json::Value& files = answer["files"];
            const json::Value& links = answer["links"];
            size_t link = 0;
            for (size_t i = 0; i < files.size(); i++) {
                const json::Value& file = files.at(i);
                if (!file["selected"].truthy()) continue;
                remote.files.push_back({ file["path"].str(), file["bytes"].u64(), links.at(link++).str() });
            }
            if (remote.files.size() != links.size()) {
                // Several files packed into one link (an archive): take the links as they are.
                remote.files.clear();
                for (size_t i = 0; i < links.size(); i++) remote.files.push_back({ "", 0, links.at(i).str() });
            }
            remote.state = Remote::Ready;
        }
        return true;
    }
    case ALL_DEBRID: {
        if (!call("GET", std::string("https://api.alldebrid.com/v4.1/magnet/status?agent=") + kAgent + "&id=" + id, auth(),
                  "", answer, status, why))
            return false;
        if (answer["status"].str() != "success") {
            const std::string code = answer["error"]["code"].str();
            if (code.find("NOT_FOUND") != std::string::npos || code == "MAGNET_INVALID_ID") {
                remote.state = Remote::Missing;
                return true;
            }
            why = serviceError(answer);
            return false;
        }
        const json::Value& magnets = answer["data"]["magnets"];
        const json::Value& magnet = magnets.isArray() ? magnets.at(0) : magnets;
        const int code = static_cast<int>(magnet["statusCode"].num());
        remote.message = magnet["status"].str();
        remote.size = magnet["size"].u64();
        remote.progress = remote.size ? static_cast<double>(magnet["downloaded"].u64()) / remote.size : 0;
        if (code >= 5) {
            remote.state = Remote::Failed;
            remote.message = std::string(name()) + ": " + remote.message;
        } else if (code == 4) {
            std::vector<std::string> headers = auth();
            headers.push_back("Content-Type: application/x-www-form-urlencoded");
            json::Value files;
            if (!call("POST", std::string("https://api.alldebrid.com/v4/magnet/files?agent=") + kAgent, headers,
                      "id[]=" + urlEncode(id), files, status, why))
                return false;
            flattenAllDebrid(files["data"]["magnets"].at(0)["files"], "", remote.files, 0);
            remote.state = remote.files.empty() ? Remote::Failed : Remote::Ready;
            if (remote.files.empty()) remote.message = std::string(name()) + " lists no files for this torrent.";
        }
        return true;
    }
    case PREMIUMIZE: {
        if (!call("GET", "https://www.premiumize.me/api/transfer/list?apikey=" + urlEncode(config_.key), {}, "", answer,
                  status, why))
            return false;
        if (answer["status"].str() != "success") {
            why = serviceError(answer);
            return false;
        }
        const json::Value& transfers = answer["transfers"];
        const json::Value* transfer = nullptr;
        for (size_t i = 0; i < transfers.size(); i++) {
            if (transfers.at(i)["id"].str() == id) transfer = &transfers.at(i);
        }
        if (!transfer) {
            remote.state = Remote::Missing;
            return true;
        }
        const std::string state = (*transfer)["status"].str();
        remote.message = (*transfer)["message"].str().empty() ? state : (*transfer)["message"].str();
        remote.progress = (*transfer)["progress"].num();
        if (state == "error" || state == "deleted" || state == "banned" || state == "timeout") {
            remote.state = Remote::Failed;
            remote.message = std::string(name()) + ": " + remote.message;
        } else if (state == "finished" || state == "seeding") {
            const std::string file = (*transfer)["file_id"].str();
            if (!file.empty()) {
                json::Value item;
                if (!call("GET", "https://www.premiumize.me/api/item/details?apikey=" + urlEncode(config_.key) + "&id=" + urlEncode(file),
                          {}, "", item, status, why))
                    return false;
                remote.files.push_back({ item["name"].str(), item["size"].u64(), item["link"].str() });
            } else if (!premiumizeFolder((*transfer)["folder_id"].str(), remote.files, why, 0)) {
                return false;
            }
            remote.state = remote.files.empty() ? Remote::Failed : Remote::Ready;
            if (remote.files.empty()) remote.message = std::string(name()) + " lists no files for this torrent.";
        }
        return true;
    }
    case TORBOX: {
        if (!call("GET", "https://api.torbox.app/v1/api/torrents/mylist?bypass_cache=true&id=" + urlEncode(id), auth(), "",
                  answer, status, why))
            return false;
        // With an id the answer is that transfer; some answers list several:
        // only the one with this id counts (never the first of the account's).
        const json::Value& data = answer["data"];
        const json::Value* found = nullptr;
        if (data.isArray()) {
            for (size_t i = 0; i < data.size(); i++) {
                if (data.at(i)["id"].str() == id) found = &data.at(i);
            }
        } else if (data.isObject() && data["id"].str() == id) {
            found = &data;
        }
        if (!found) {
            if (status == 404 || answer["success"].truthy() || data.isNull()) {
                remote.state = Remote::Missing; // not listed (yet)
                return true;
            }
            why = serviceError(answer);
            return false;
        }
        const json::Value& torrent = *found;
        const std::string state = torrent["download_state"].str();
        remote.message = state;
        remote.progress = torrent["progress"].num();
        remote.size = torrent["size"].u64();
        if (state.find("error") != std::string::npos || state.find("failed") != std::string::npos) {
            remote.state = Remote::Failed;
            remote.message = std::string(name()) + " could not download this torrent (" + state + ").";
        } else if (torrent["download_present"].truthy() || torrent["download_finished"].truthy()) {
            const json::Value& files = torrent["files"];
            for (size_t i = 0; i < files.size(); i++) {
                remote.files.push_back({ files.at(i)["name"].str(), files.at(i)["size"].u64(), files.at(i)["id"].str() });
            }
            remote.state = remote.files.empty() ? Remote::Waiting : Remote::Ready;
        }
        return true;
    }
    case DEBRID_LINK: {
        if (!call("GET", "https://debrid-link.com/api/v2/seedbox/list?ids=" + urlEncode(id), auth(), "", answer, status,
                  why))
            return false;
        if (!answer["success"].truthy()) {
            why = serviceError(answer);
            return false;
        }
        const json::Value& torrent = answer["value"].at(0);
        if (!torrent.isObject()) {
            remote.state = Remote::Missing;
            return true;
        }
        remote.progress = torrent["downloadPercent"].num() / 100.0;
        remote.size = torrent["totalSize"].u64();
        remote.message = "downloading";
        if (torrent["errorId"].truthy()) {
            remote.state = Remote::Failed;
            remote.message = std::string(name()) + " could not download this torrent.";
        } else if (remote.progress >= 1.0) {
            const json::Value& files = torrent["files"];
            for (size_t i = 0; i < files.size(); i++) {
                remote.files.push_back(
                    { files.at(i)["name"].str(), files.at(i)["size"].u64(), files.at(i)["downloadUrl"].str() });
            }
            remote.state = remote.files.empty() ? Remote::Waiting : Remote::Ready;
        }
        return true;
    }
    default:
        why = "No Debrid service is chosen.";
        return false;
    }
}

bool Client::link(const std::string& id, const RemoteFile& file, std::string& url, std::string& why)
{
    json::Value answer;
    int status = 0;
    switch (config_.service) {
    case REAL_DEBRID: {
        std::vector<std::string> headers = auth();
        headers.push_back("Content-Type: application/x-www-form-urlencoded");
        if (!call("POST", "https://api.real-debrid.com/rest/1.0/unrestrict/link", headers, "link=" + urlEncode(file.ref),
                  answer, status, why))
            return false;
        url = answer["download"].str();
        break;
    }
    case ALL_DEBRID:
        if (!call("GET", std::string("https://api.alldebrid.com/v4/link/unlock?agent=") + kAgent + "&link=" + urlEncode(file.ref),
                  auth(), "", answer, status, why))
            return false;
        url = answer["data"]["link"].str();
        break;
    case TORBOX:
        if (!call("GET", "https://api.torbox.app/v1/api/torrents/requestdl?token=" + urlEncode(config_.key) +
                             "&torrent_id=" + urlEncode(id) + "&file_id=" + urlEncode(file.ref) + "&zip_link=false",
                  auth(), "", answer, status, why))
            return false;
        url = answer["data"].str();
        break;
    default: // Premiumize and Debrid-Link list direct addresses
        url = file.ref;
        return !url.empty();
    }
    if (url.compare(0, 8, "https://") != 0 && url.compare(0, 7, "http://") != 0) {
        why = serviceError(answer);
        url.clear();
        return false;
    }
    return true;
}

// ---- The transfer -------------------------------------------------------------

struct Run {
    const DebridNet& net;
    const torrent_callbacks* cb;
    double lastReport = 0;

    bool stopped() const { return cb->stopped && cb->stopped(cb->user); }
    void report(const std::string& phase, uint64_t done, uint64_t total, bool force = false)
    {
        const double now = secondsNow();
        if (force || now - lastReport >= 0.5) {
            lastReport = now;
            if (cb->progress) cb->progress(cb->user, phase.c_str(), done, total, 0);
        }
    }
    void fail(const std::string& why) const
    {
        if (cb->error) cb->error(cb->user, why.c_str());
    }
    // Waits, checking for a pause; false when stopped.
    bool wait(int milliseconds) const
    {
        for (int waited = 0; waited < milliseconds; waited += 250) {
            if (stopped()) return false;
            if (net.sleep) net.sleep(250);
        }
        return !stopped();
    }
};

uint64_t fileSize(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
}

// ---- Downloading the files ----------------------------------------------------

constexpr uint64_t kPart = 32ull * 1024 * 1024; // a large file comes in parts of this size...
constexpr int kStreams = 4;                       // ...this many at a time

bool exists(const std::string& path)
{
    if (FILE* f = fopen(path.c_str(), "rb")) {
        fclose(f);
        return true;
    }
    return false;
}

// Positions a stream for writing at `offset` (the file grows as needed).
bool seekTo(FILE* file, uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(file, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool truncateTo(const std::string& path, uint64_t size)
{
    FILE* file = fopen(path.c_str(), "r+b");
    if (!file) return false;
#ifdef _WIN32
    const bool ok = _chsize_s(_fileno(file), static_cast<long long>(size)) == 0;
#else
    const bool ok = ftruncate(fileno(file), static_cast<off_t>(size)) == 0;
#endif
    fclose(file);
    return ok;
}

// A lock and a thread: pthreads on the console (with a stack roomy enough for
// TLS), the standard library on the PC.
class Lock {
public:
#ifdef _WIN32
    void lock() { mutex_.lock(); }
    void unlock() { mutex_.unlock(); }
private:
    std::mutex mutex_;
#else
    Lock() { pthread_mutex_init(&mutex_, nullptr); }
    ~Lock() { pthread_mutex_destroy(&mutex_); }
    void lock() { pthread_mutex_lock(&mutex_); }
    void unlock() { pthread_mutex_unlock(&mutex_); }
private:
    pthread_mutex_t mutex_;
#endif
};

class Worker {
public:
    explicit Worker(std::function<void()> body) : body_(std::move(body))
    {
#ifdef _WIN32
        thread_ = std::thread(body_);
        started_ = true;
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 512 * 1024);
        started_ = pthread_create(&thread_, &attr, entry, this) == 0;
        pthread_attr_destroy(&attr);
#endif
    }
    ~Worker() { join(); }
    bool started() const { return started_; }
    void join()
    {
        if (!started_) return;
#ifdef _WIN32
        thread_.join();
#else
        pthread_join(thread_, nullptr);
#endif
        started_ = false;
    }

private:
#ifdef _WIN32
    std::thread thread_;
#else
    pthread_t thread_;
    static void* entry(void* self)
    {
        static_cast<Worker*>(self)->body_();
        return nullptr;
    }
#endif
    std::function<void()> body_;
    bool started_ = false;
};

struct Transfer {
    const DebridNet& net;
    Client& client;
    Run& run;
    const std::string& id;
    const std::string& service;
    const torrent_callbacks* cb;
    uint64_t received = 0; // bytes this run, for the speed shown

    // One connection after the other, resumed from the file's end.
    // 0 done, 1 stopped, -1 failed.
    int sequential(const RemoteFile& file, std::string& path, const std::string& destination, const std::string& phase,
                   uint64_t done, uint64_t& total)
    {
        uint64_t have = path.empty() ? 0 : fileSize(path);
        if (!path.empty() && file.size && have == file.size && !exists(path + ".parts")) return 0;
        if (file.size && have > file.size) {
            remove(path.c_str());
            have = 0;
        }
        std::string why;
        int ignoredRanges = 0;
        for (int attempt = 0; attempt < kAttempts; attempt++) {
            const uint64_t before = have;
            if (run.stopped()) return 1;
            std::string url;
            if (!client.link(id, file, url, why)) {
                if (attempt + 1 >= kAttempts) break;
                run.report(why, 0, 0, true);
                if (!run.wait(kPollMs)) return 1;
                continue;
            }
            if (path.empty()) {
                // A link without a known name: the last part of its address.
                path = destination + "/" + safeName(url.substr(0, url.find('?')));
                have = fileSize(path);
            }
            FILE* out = nullptr;
            uint64_t written = have, skip = 0;
            bool oversized = false;
            const auto begin = [&](uint64_t start, uint64_t size) {
                if (start != 0 && start != have) return false;
                if (start == 0 && have > 0) {
                    // The server sends the whole file again: first ask another
                    // server; then keep what is here and skip the file's start.
                    if (++ignoredRanges <= 2) return false;
                    skip = have;
                }
                out = fopen(path.c_str(), have > 0 ? "ab" : "wb");
                if (out) setvbuf(out, nullptr, _IOFBF, 1 << 20);
                written = have;
                if (size && !file.size && start == 0) total += size; // a size learnt only now
                return out != nullptr;
            };
            const auto write = [&](const unsigned char* data, size_t size) {
                if (skip) {
                    const size_t dropped = static_cast<size_t>(std::min<uint64_t>(skip, size));
                    skip -= dropped;
                    data += dropped;
                    size -= dropped;
                    if (!size) return !run.stopped();
                }
                if (file.size && written + size > file.size) {
                    oversized = true; // not the file the service listed
                    return false;
                }
                if (!out || fwrite(data, 1, size, out) != size) return false;
                written += size;
                received += size;
                if (cb->traffic) cb->traffic(cb->user, received);
                run.report(phase, done + written, std::max(total, done + written));
                return !run.stopped();
            };
            const auto stopped = [this] { return run.stopped(); };
            std::string failure;
            const bool ok = net.download(url, have, 0, begin, write, stopped, failure);
            const bool closed = out ? fclose(out) == 0 : true;
            if (run.stopped()) return 1;
            have = fileSize(path);
            if (ok && closed && (!file.size || have >= file.size)) {
                run.report(phase, done + have, std::max(total, done + have), true);
                return 0;
            }
            if (oversized) {
                remove(path.c_str());
                run.fail(service + " sent a file larger than it listed: " + safeName(path) + ".");
                return -1;
            }
            if (have > before) {
                attempt = -1; // it moved on: ask for the rest at once
                continue;
            }
            why = "The download from " + service + " was interrupted (" + failure + "); retrying.";
            run.report(why, 0, 0, true);
            if (!run.wait(3000 * (attempt + 1))) return 1;
        }
        run.fail(why.empty() ? "Could not download " + safeName(file.name) + " from " + service + "." : why);
        return -1;
    }

    // Several connections, each on a part of its own. Which parts arrived is
    // kept in <file>.parts ('1' per part), so a resumed download asks only
    // for the rest. 0 done, 1 stopped, -1 failed, 2 the server does not send
    // parts (the caller downloads the file in one piece).
    int parallel(const RemoteFile& file, const std::string& path, const std::string& phase, uint64_t done,
                 uint64_t total)
    {
        const size_t count = static_cast<size_t>((file.size + kPart - 1) / kPart);
        const std::string partsPath = path + ".parts";
        std::string parts = readFile(partsPath);
        const uint64_t have = fileSize(path);
        if (parts.size() != count) {
            if (have == file.size && !exists(partsPath)) return 0; // complete before
            if (have > file.size) remove(path.c_str());
            // A file begun in one piece: the whole parts it holds count.
            parts.assign(count, '0');
            for (size_t i = 0; i < count && (i + 1) * kPart <= have && have <= file.size; i++) parts[i] = '1';
        }
        if (!exists(path)) {
            if (FILE* create = fopen(path.c_str(), "wb")) fclose(create);
        }
        if (!writeFile(partsPath, parts)) return 2;

        std::string url, why;
        if (!client.link(id, file, url, why)) {
            run.report(why, 0, 0, true);
            return 2; // the one-piece path asks again, and waits
        }

        Lock lock;
        std::vector<char> taken(count, 0);
        std::vector<int> failures(count, 0);
        std::vector<uint64_t> inFlight(kStreams, 0);
        bool stop = false, refused = false, failed = false;
        int running = 0, linkGeneration = 0;

        const auto stream = [&](int slot) {
            FILE* out = fopen(path.c_str(), "r+b");
            if (out) setvbuf(out, nullptr, _IOFBF, 1 << 20);
            for (;;) {
                size_t part = count;
                std::string address;
                int generation = 0;
                {
                    std::lock_guard<Lock> guard(lock);
                    if (!out) {
                        failed = true;
                        why = "Cannot open " + safeName(path) + " for writing.";
                    }
                    if (stop || failed || refused) break;
                    for (size_t i = 0; i < count && part == count; i++) {
                        if (parts[i] != '1' && !taken[i]) part = i;
                    }
                    if (part == count) break; // the rest is with the other connections
                    taken[part] = 1;
                    address = url;
                    generation = linkGeneration;
                }
                const uint64_t from = part * kPart;
                const uint64_t to = std::min<uint64_t>(file.size, from + kPart);
                uint64_t at = from;
                bool diskError = false;
                while (at < to) {
                    const uint64_t before = at;
                    const auto begin = [&](uint64_t start, uint64_t) {
                        if (start != at) {
                            std::lock_guard<Lock> guard(lock);
                            refused = true; // the whole file instead of the part
                            return false;
                        }
                        return seekTo(out, at);
                    };
                    const auto write = [&](const unsigned char* data, size_t size) {
                        if (at + size > to) {
                            // More than the part: the server ignored the range.
                            std::lock_guard<Lock> guard(lock);
                            refused = true;
                            return false;
                        }
                        if (fwrite(data, 1, size, out) != size) {
                            diskError = true;
                            return false;
                        }
                        at += size;
                        std::lock_guard<Lock> guard(lock);
                        inFlight[slot] = at - from;
                        received += size;
                        return !stop;
                    };
                    const auto stopped = [&] {
                        std::lock_guard<Lock> guard(lock);
                        return stop;
                    };
                    std::string failure;
                    net.download(address, at, to, begin, write, stopped, failure);
                    fflush(out);
                    std::unique_lock<Lock> guard(lock);
                    if (stop || refused || diskError) break;
                    if (at > before) continue; // the server sends parts of parts: ask for the rest
                    // Nothing came: maybe the address expired. Ask for a new one.
                    if (++failures[part] >= kAttempts) {
                        failed = true;
                        why = "The download from " + service + " keeps failing (" + failure + ").";
                        break;
                    }
                    if (generation == linkGeneration) {
                        guard.unlock();
                        std::string fresh, error;
                        const bool got = client.link(id, file, fresh, error);
                        guard.lock();
                        if (got && generation == linkGeneration) {
                            url = fresh;
                            ++linkGeneration;
                        }
                    }
                    address = url;
                    generation = linkGeneration;
                    guard.unlock();
                    if (net.sleep) net.sleep(1000);
                }
                std::lock_guard<Lock> guard(lock);
                inFlight[slot] = 0;
                taken[part] = 0;
                if (diskError) {
                    failed = true;
                    why = "Disk write failed. Check free space.";
                }
                if (at == to) {
                    parts[part] = '1';
                    writeFile(partsPath, parts);
                }
            }
            if (out) fclose(out);
            std::lock_guard<Lock> guard(lock);
            --running;
        };

        std::vector<std::unique_ptr<Worker>> workers;
        {
            std::lock_guard<Lock> guard(lock);
            running = kStreams;
        }
        for (int slot = 0; slot < kStreams; slot++) {
            workers.emplace_back(new Worker([&stream, slot] { stream(slot); }));
            if (!workers.back()->started()) {
                std::lock_guard<Lock> guard(lock);
                --running;
            }
        }
        uint64_t lastReceived = 0;
        bool complete = false;
        for (;;) {
            uint64_t arrived = 0, moved = 0;
            bool over = false;
            {
                std::lock_guard<Lock> guard(lock);
                size_t whole = 0;
                for (size_t i = 0; i < count; i++) {
                    if (parts[i] == '1') {
                        arrived += std::min<uint64_t>(kPart, file.size - i * kPart);
                        whole++;
                    }
                }
                for (uint64_t bytes : inFlight) arrived += bytes;
                moved = received;
                complete = whole == count;
                over = running == 0;
            }
            if (moved != lastReceived && cb->traffic) cb->traffic(cb->user, moved);
            lastReceived = moved;
            run.report(phase, done + arrived, std::max(total, done + file.size));
            if (over) break;
            if (run.stopped()) {
                std::lock_guard<Lock> guard(lock);
                stop = true;
            }
            if (net.sleep) net.sleep(250);
        }
        workers.clear(); // joins
        if (run.stopped()) return 1;
        if (complete) {
            remove(partsPath.c_str());
            return 0;
        }
        if (refused) {
            // Keep the parts that arrived in a row from the start and go on in one piece.
            size_t prefix = 0;
            while (prefix < count && parts[prefix] == '1') prefix++;
            const uint64_t keep = std::min<uint64_t>(file.size, prefix * kPart);
            if (keep == 0) {
                remove(path.c_str());
            } else {
                truncateTo(path, keep);
            }
            remove(partsPath.c_str());
            return 2;
        }
        run.fail(why.empty() ? "Could not download " + safeName(path) + " from " + service + "." : why);
        return -1;
    }
};


} // namespace

const char* debridServiceName(int service)
{
    return kNames[service >= 0 && service < kServices ? service : 0];
}

DebridConfig debridConfig(const std::string& dataRoot)
{
    DebridConfig config;
    const std::string settings = readFile(dataRoot + "/settings.txt");
    const size_t at = settings.find("debrid=");
    if (at != std::string::npos && (at == 0 || settings[at - 1] == '\n')) {
        config.service = atoi(settings.c_str() + at + 7);
    }
    if (config.service <= 0 || config.service >= kServices) {
        config.service = 0;
        return config;
    }
    const std::string keys = readFile(dataRoot + "/debrid-keys.txt");
    const std::string prefix = std::to_string(config.service) + "=";
    for (size_t from = 0; from < keys.size();) {
        size_t end = keys.find('\n', from);
        if (end == std::string::npos) end = keys.size();
        if (keys.compare(from, prefix.size(), prefix) == 0) {
            config.key = keys.substr(from + prefix.size(), end - from - prefix.size());
            while (!config.key.empty() && (config.key.back() == '\r' || config.key.back() == ' ')) config.key.pop_back();
        }
        from = end + 1;
    }
    return config;
}

std::string debridMagnet(const std::string& metainfo)
{
    char hash[41];
    if (torrent_identity(reinterpret_cast<const unsigned char*>(metainfo.data()), metainfo.size(), hash) != 0) {
        return std::string();
    }
    const TorrentFacts facts = torrentFacts(metainfo);
    std::string magnet = std::string("magnet:?xt=urn:btih:") + hash;
    if (!facts.name.empty()) magnet += "&dn=" + urlEncode(facts.name);
    for (const std::string& tracker : facts.trackers) magnet += "&tr=" + urlEncode(tracker);
    return magnet;
}

int debridRun(const DebridNet& net, const DebridConfig& config, const std::string& metainfo,
              const std::string& statePath, const std::string& destination, const torrent_callbacks* callbacks)
{
    Run run{ net, callbacks };
    Client client(net, config);
    const std::string service = client.name();
    if (!config.enabled() || !net.request || !net.download) {
        run.fail("Debrid is not set up. Choose a service and its API key in Settings.");
        return -1;
    }
    mkdir(destination.c_str(), 0777);

    // The transfer at the service: the one added before, or a new one.
    std::string id;
    {
        const std::string saved = readFile(statePath);
        const std::string mark = std::to_string(config.service) + "\n";
        if (saved.compare(0, mark.size(), mark) == 0) {
            id = saved.substr(mark.size());
            while (!id.empty() && (id.back() == '\n' || id.back() == '\r')) id.pop_back();
        }
    }
    std::string why;
    Remote remote;
    int misses = 0, readds = 0;
    for (int attempt = 0;; attempt++) {
        if (run.stopped()) return 1;
        if (id.empty()) {
            run.report("Sending the torrent to " + service, 0, 0, true);
            if (!client.add(metainfo, id, why)) {
                run.fail(why);
                return -1;
            }
            writeFile(statePath, std::to_string(config.service) + "\n" + id + "\n");
        }
        if (!client.status(id, remote, why)) {
            // A passing network problem: wait and ask again.
            if (attempt >= 20) {
                run.fail(why);
                return -1;
            }
            run.report(why, 0, 0, true);
            if (!run.wait(kPollMs)) return 1;
            continue;
        }
        if (remote.state == Remote::Missing) {
            // Just added, a service may not list it for a while; one that
            // stays missing was removed there: add it again (twice at most).
            if (++misses < 6) {
                run.report("Sending the torrent to " + service, 0, 0, true);
                if (!run.wait(kPollMs)) return 1;
                continue;
            }
            misses = 0;
            if (++readds > 2) {
                run.fail(service + " lost the transfer.");
                return -1;
            }
            id.clear();
            continue;
        }
        misses = 0;
        if (remote.state == Remote::Failed) {
            remove(statePath.c_str());
            run.fail(remote.message);
            return -1;
        }
        if (remote.state == Remote::Ready) break;
        // Still downloading at the service.
        char percent[16];
        snprintf(percent, sizeof percent, "%.0f%%", std::max(0.0, std::min(1.0, remote.progress)) * 100);
        const uint64_t total = remote.size;
        (void)total;
        run.report(service + " is downloading the torrent: " + percent +
                       (remote.message.empty() ? std::string() : " (" + remote.message + ")"),
                   0, 0, true); // text only: the bar shows this console's own download
        attempt = 0;
        if (!run.wait(kPollMs)) return 1;
    }

    // The files, one after the other. A large file comes in parts over
    // several connections at once; any file resumes where it stopped.
    uint64_t total = 0, done = 0;
    for (const RemoteFile& file : remote.files) total += file.size;
    Transfer transfer{ net, client, run, id, service, callbacks };
    for (size_t index = 0; index < remote.files.size(); index++) {
        const RemoteFile& file = remote.files[index];
        std::string path = file.name.empty() ? std::string() : destination + "/" + safeName(file.name);
        const std::string phase = "Downloading from " + service + " (" + std::to_string(index + 1) + "/" +
                                  std::to_string(remote.files.size()) + "): " + safeName(path.empty() ? file.ref : path);
        int result = 2;
        if (!path.empty() && file.size >= 2 * kPart) {
            result = transfer.parallel(file, path, phase, done, total);
        }
        if (result == 2) { // small, of unknown size, or the server does not send parts
            result = transfer.sequential(file, path, destination, phase, done, total);
        }
        if (result != 0) return result;
        done += file.size ? file.size : fileSize(path);
        total = std::max(total, done);
    }
    run.report("Downloaded from " + service, done, std::max(total, done), true);
    return 0;
}
