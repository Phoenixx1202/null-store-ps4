// Debrid transfers against scripted services: TorBox, Real-Debrid and
// AllDebrid answer as their documentation and probes show; the files are
// "downloaded" from memory, interrupted once to check the resume.
#include "../src/debrid.h"
#include "../src/catalog.h"
#include "../src/json_lite.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <sys/stat.h>

extern "C" int torrent_identity(const unsigned char*, size_t, char out[41])
{
    strcpy(out, "0123456789abcdef0123456789abcdef01234567");
    return 0;
}

namespace {

struct Call {
    std::string method, url, body;
    std::vector<std::string> headers;
};

struct Script {
    std::vector<Call> calls;
    std::map<std::string, std::string> files; // address -> contents
    std::function<std::string(const Call&, int&)> answer;
    int interruptions = 1; // the first download stops half way
    size_t chunk = 0;      // > 0: every answer carries at most this many bytes, then ends normally
    bool ignoreRange = false; // the server always sends the whole file
    std::mutex lock;          // the parallel download calls from several threads
    std::string phase, error;
    uint64_t done = 0, total = 0;
};

Script* script = nullptr;

DebridNet network()
{
    DebridNet net;
    net.request = [](const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                     const std::string& body, std::string& out, int& status) {
        Call call{ method, url, body, headers };
        std::lock_guard<std::mutex> guard(script->lock);
        script->calls.push_back(call);
        status = 200;
        out = script->answer(call, status);
        return true;
    };
    net.download = [](const std::string& url, uint64_t offset, uint64_t last,
                      const std::function<bool(uint64_t, uint64_t)>& begin,
                      const std::function<bool(const unsigned char*, size_t)>& write, const std::function<bool()>&,
                      std::string& why) {
        std::unique_lock<std::mutex> guard(script->lock);
        const auto found = script->files.find(url);
        if (found == script->files.end()) {
            why = "http 404";
            return false;
        }
        const std::string& data = found->second;
        if (script->ignoreRange) {
            offset = 0;
            last = 0;
        }
        const size_t stop = last ? static_cast<size_t>(last) : data.size();
        bool interrupted = false;
        if (script->interruptions > 0) {
            script->interruptions--;
            interrupted = true;
        }
        const size_t chunk = script->chunk;
        guard.unlock();
        if (!begin(offset, data.size())) return false;
        size_t end = stop;
        if (interrupted) {
            end = offset + (stop - offset) / 2;
        } else if (chunk && offset + chunk < stop) {
            if (!write(reinterpret_cast<const unsigned char*>(data.data() + offset), chunk)) return false;
            return true; // a whole answer, but only part of the file
        }
        if (!write(reinterpret_cast<const unsigned char*>(data.data() + offset), end - offset)) return false;
        if (end < stop) {
            why = "body_read";
            return false;
        }
        return true;
    };
    net.sleep = [](int) {};
    return net;
}

torrent_callbacks callbacks()
{
    torrent_callbacks cb = {};
    cb.user = nullptr;
    cb.stopped = [](void*) { return 0; };
    cb.progress = [](void*, const char* phase, uint64_t done, uint64_t total, unsigned) {
        script->phase = phase;
        script->done = done;
        script->total = total;
    };
    cb.error = [](void*, const char* why) { script->error = why; };
    return cb;
}

bool has(const Call& call, const char* text)
{
    return call.url.find(text) != std::string::npos;
}

void clean(const std::string& dir)
{
    remove((dir + "/game.pkg").c_str());
    remove((dir + "/update.pkg").c_str());
    remove("build/debrid-test/state");
}

} // namespace

int main()
{
    mkdir("build", 0777);
    mkdir("build/debrid-test", 0777);
    const std::string dir = "build/debrid-test/out";
    mkdir(dir.c_str(), 0777);
    const std::string meta = "d8:announce21:udp://tracker.test:804:infod4:name9:Game Test6:lengthi1eee";
    const DebridNet net = network();

    // JSON reader.
    {
        const json::Value v = json::parse("{\"a\":[1,{\"b\":\"x\\u00e9\\n\"}],\"c\":true,\"d\":null,\"e\":12345678901234}");
        assert(v["a"].at(1)["b"].str() == "x\xC3\xA9\n" && v["c"].truthy() && v["d"].isNull() && v["e"].u64() == 12345678901234ULL);
        assert(json::parse("{broken").isNull());
    }
    // Magnet link: hash, name and trackers.
    assert(debridMagnet(meta) == "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=Game%20Test&tr=udp%3A%2F%2Ftracker.test%3A80");

    // Settings as the store writes them.
    writeFile("build/debrid-test/settings.txt", "source=0\nsort=0\nofficial-art=1\ndebrid=4\n");
    writeFile("build/debrid-test/debrid-keys.txt", "1=rd-key\n4=tb-key\n");
    {
        const DebridConfig config = debridConfig("build/debrid-test");
        assert(config.service == 4 && config.key == "tb-key" && config.enabled());
        assert(std::string(debridServiceName(4)) == "TorBox");
    }

    // TorBox: upload the torrent file, wait while it downloads, then each file.
    {
        Script s;
        script = &s;
        int polls = 0;
        s.files["https://cdn.tb/game"] = std::string(1000, 'g');
        s.files["https://cdn.tb/update"] = std::string(300, 'u');
        s.answer = [&](const Call& call, int&) -> std::string {
            if (has(call, "/torrents/createtorrent")) {
                assert(call.method == "POST" && call.body.find("filename=\"game.torrent\"") != std::string::npos &&
                       call.body.find(meta) != std::string::npos);
                bool bearer = false;
                for (auto& h : call.headers) bearer = bearer || h == "Authorization: Bearer tb-key";
                assert(bearer);
                return "{\"success\":true,\"error\":null,\"detail\":\"ok\",\"data\":{\"torrent_id\":777,\"hash\":\"x\"}}";
            }
            if (has(call, "/torrents/mylist")) {
                assert(has(call, "id=777"));
                if (polls == 0) {
                    polls++;
                    return "{\"success\":false,\"error\":\"NOT_FOUND\",\"detail\":\"Torrent not found\",\"data\":null}";
                }
                if (polls++ < 3)
                    return "{\"success\":true,\"data\":{\"id\":777,\"download_state\":\"downloading\",\"progress\":0.5,\"size\":1300,"
                           "\"download_finished\":false,\"download_present\":false,\"files\":[]}}";
                return "{\"success\":true,\"data\":[{\"id\":5,\"download_state\":\"completed\",\"download_present\":true,"
                       "\"files\":[{\"id\":0,\"name\":\"Other/other.pkg\",\"size\":9}]},{\"id\":777,\"download_state\":\"completed\",\"progress\":1,\"size\":1300,"
                       "\"download_finished\":true,\"download_present\":true,\"files\":["
                       "{\"id\":0,\"name\":\"Game Test/game.pkg\",\"size\":1000},{\"id\":1,\"name\":\"Game Test/update.pkg\",\"size\":300}]}]}";
            }
            if (has(call, "/torrents/requestdl")) {
                assert(has(call, "token=tb-key") && has(call, "torrent_id=777"));
                return has(call, "file_id=0") ? "{\"success\":true,\"data\":\"https://cdn.tb/game\"}"
                                              : "{\"success\":true,\"data\":\"https://cdn.tb/update\"}";
            }
            assert(!"unexpected TorBox call");
            return "";
        };
        clean(dir);
        torrent_callbacks cb = callbacks();
        const int result = debridRun(net, debridConfig("build/debrid-test"), meta, "build/debrid-test/state", dir, &cb);
        assert(result == 0);
        assert(readFile(dir + "/game.pkg") == std::string(1000, 'g') && readFile(dir + "/update.pkg") == std::string(300, 'u'));
        assert(s.done == 1300 && s.total == 1300 && polls == 4);
        assert(readFile("build/debrid-test/state") == "4\n777\n");

        // Run again: the transfer is reused and the complete files are kept.
        const size_t before = s.calls.size();
        assert(debridRun(net, debridConfig("build/debrid-test"), meta, "build/debrid-test/state", dir, &cb) == 0);
        for (size_t i = before; i < s.calls.size(); i++) assert(!has(s.calls[i], "createtorrent") && !has(s.calls[i], "requestdl"));
    }

    // TorBox refuses the key.
    {
        Script s;
        script = &s;
        s.answer = [](const Call&, int& status) -> std::string {
            status = 403;
            return "{\"success\":false,\"error\":\"BAD_TOKEN\",\"detail\":\"Your token is invalid\",\"data\":null}";
        };
        clean(dir);
        torrent_callbacks cb = callbacks();
        assert(debridRun(net, debridConfig("build/debrid-test"), meta, "build/debrid-test/state", dir, &cb) == -1);
        assert(s.error == "TorBox refused the API key. Check it in Settings.");
    }

    // Real-Debrid: add, select the files, unrestrict each link.
    {
        writeFile("build/debrid-test/settings.txt", "debrid=1\n");
        Script s;
        script = &s;
        s.interruptions = 0;
        s.chunk = 64; // the file comes in eight parts
        bool selected = false;
        s.files["https://dl.rd/game.pkg"] = std::string(500, 'r');
        s.answer = [&](const Call& call, int& status) -> std::string {
            if (has(call, "/torrents/addTorrent")) {
                assert(call.method == "PUT" && call.body == meta);
                status = 201;
                return "{\"id\":\"RDID\",\"uri\":\"https://api.real-debrid.com/rest/1.0/torrents/info/RDID\"}";
            }
            if (has(call, "/torrents/info/RDID")) {
                if (!selected)
                    return "{\"id\":\"RDID\",\"status\":\"waiting_files_selection\",\"progress\":0,\"bytes\":500,\"files\":[],\"links\":[]}";
                return "{\"id\":\"RDID\",\"status\":\"downloaded\",\"progress\":100,\"bytes\":500,"
                       "\"files\":[{\"id\":1,\"path\":\"/Game/game.pkg\",\"bytes\":500,\"selected\":1},"
                       "{\"id\":2,\"path\":\"/Game/readme.txt\",\"bytes\":10,\"selected\":0}],"
                       "\"links\":[\"https://real-debrid.com/d/ABC\"]}";
            }
            if (has(call, "/torrents/selectFiles/RDID")) {
                assert(call.body == "files=all");
                selected = true;
                status = 204;
                return "";
            }
            if (has(call, "/unrestrict/link")) {
                assert(call.body == "link=https%3A%2F%2Freal-debrid.com%2Fd%2FABC");
                return "{\"filename\":\"game.pkg\",\"filesize\":500,\"download\":\"https://dl.rd/game.pkg\"}";
            }
            assert(!"unexpected Real-Debrid call");
            return "";
        };
        clean(dir);
        torrent_callbacks cb = callbacks();
        assert(debridRun(net, debridConfig("build/debrid-test"), meta, "build/debrid-test/state", dir, &cb) == 0);
        assert(readFile(dir + "/game.pkg") == std::string(500, 'r') && selected);
    }

    // AllDebrid: upload, status 4 (ready), file tree, unlock.
    {
        writeFile("build/debrid-test/settings.txt", "debrid=2\n");
        writeFile("build/debrid-test/debrid-keys.txt", "2=ad-key\n");
        Script s;
        script = &s;
        s.interruptions = 0;
        s.files["https://dl.ad/update"] = std::string(64, 'a');
        s.answer = [&](const Call& call, int&) -> std::string {
            if (has(call, "/v4/magnet/upload/file")) {
                assert(has(call, "agent=NULL_store") && call.body.find("name=\"files[0]\"") != std::string::npos);
                return "{\"status\":\"success\",\"data\":{\"files\":[{\"file\":\"game.torrent\",\"id\":42,\"ready\":true}]}}";
            }
            if (has(call, "/v4.1/magnet/status"))
                return "{\"status\":\"success\",\"data\":{\"magnets\":{\"id\":42,\"status\":\"Ready\",\"statusCode\":4,\"size\":64,\"downloaded\":64}}}";
            if (has(call, "/v4/magnet/files")) {
                assert(call.body == "id[]=42");
                return "{\"status\":\"success\",\"data\":{\"magnets\":[{\"id\":\"42\",\"files\":[{\"n\":\"Game\",\"e\":[{\"n\":\"update.pkg\",\"s\":64,\"l\":\"https://alldebrid.com/f/X\"}]}]}]}}";
            }
            if (has(call, "/v4/link/unlock"))
                return "{\"status\":\"success\",\"data\":{\"link\":\"https://dl.ad/update\",\"filename\":\"update.pkg\",\"filesize\":64}}";
            assert(!"unexpected AllDebrid call");
            return "";
        };
        clean(dir);
        torrent_callbacks cb = callbacks();
        assert(debridRun(net, debridConfig("build/debrid-test"), meta, "build/debrid-test/state", dir, &cb) == 0);
        assert(readFile(dir + "/update.pkg") == std::string(64, 'a'));
    }

    // A large file: parts over four connections, two of them interrupted; then
    // a server that ignores ranges (the file comes in one piece instead).
    for (int ignore = 0; ignore < 2; ignore++) {
        writeFile("build/debrid-test/settings.txt", "debrid=4\n");
        writeFile("build/debrid-test/debrid-keys.txt", "4=tb-key\n");
        Script s;
        script = &s;
        s.interruptions = ignore ? 0 : 2;
        s.ignoreRange = ignore != 0;
        std::string big(70u * 1024 * 1024 + 123, '\0');
        for (size_t i = 0; i < big.size(); i++) big[i] = static_cast<char>((i * 7 + i / 4096) % 251);
        s.files["https://cdn.tb/big"] = big;
        s.answer = [&](const Call& call, int&) -> std::string {
            if (has(call, "/torrents/createtorrent")) return "{\"success\":true,\"data\":{\"torrent_id\":9}}";
            if (has(call, "/torrents/mylist"))
                return "{\"success\":true,\"data\":{\"id\":9,\"download_state\":\"cached\",\"download_present\":true,"
                       "\"size\":" + std::to_string(big.size()) + ",\"files\":[{\"id\":3,\"name\":\"Big/big.pkg\",\"size\":" +
                       std::to_string(big.size()) + "}]}}";
            if (has(call, "/torrents/requestdl")) return "{\"success\":true,\"data\":\"https://cdn.tb/big\"}";
            assert(!"unexpected call");
            return "";
        };
        remove((dir + "/big.pkg").c_str());
        remove((dir + "/big.pkg.parts").c_str());
        remove("build/debrid-test/state");
        torrent_callbacks cb = callbacks();
        assert(debridRun(net, debridConfig("build/debrid-test"), meta, "build/debrid-test/state", dir, &cb) == 0);
        assert(readFile(dir + "/big.pkg") == big);
        assert(readFile(dir + "/big.pkg.parts").empty());
        assert(s.done == big.size());
        remove((dir + "/big.pkg").c_str());
    }

    clean(dir);
    std::cout << "Debrid: JSON, magnet, settings, TorBox (wait, resume, reuse, bad key, parallel parts, ranges ignored), "
                 "Real-Debrid and AllDebrid passed.\n";
}
