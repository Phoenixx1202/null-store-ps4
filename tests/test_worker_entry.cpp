#define main workerEntry
#include "../src/worker_entry.cpp"
#undef main

#include <cassert>
#include <cstdlib>
#include "../src/worker_launch_attempt.h"

static int starts = 0, exits = 0;
int runPackagedWorker() {
    ++starts;
    return 17;
}
extern "C" int32_t sceSystemServiceLoadExec(const char* path, const char* args[]) {
    assert(std::strcmp(path, "exit") == 0 && args == nullptr);
    ++exits;
    return 0;
}

int main() {
    char executable[] = "/app0/eboot.bin";
    char expected[sizeof kWorkerStartArgument];
    std::strcpy(expected, kWorkerStartArgument);
    char unrelated[] = "--start";
    char* home[] = { executable, nullptr };
    char* other[] = { executable, unrelated, nullptr };
    char* extras[] = { executable, unrelated, expected, nullptr };
    assert(workerEntry(0, nullptr) == 0);
    assert(workerEntry(1, home) == 0);
    assert(workerEntry(2, other) == 0);
    assert(workerEntry(3, extras) == 0);
    assert(starts == 0 && exits == 4);

    char* launch[] = { expected, nullptr };
    char* prefixed[] = { executable, expected, nullptr };
    assert(workerEntry(1, launch) == 17);
    assert(workerEntry(2, prefixed) == 17);
    assert(starts == 2 && exits == 6);

    char folder[] = "/tmp/nullstore-worker-launch-XXXXXX";
    assert(mkdtemp(folder));
    const std::string pending = std::string(folder) + "/worker.pending";
    std::string error;
    assert(recordWorkerLaunch(folder, pending, error));
    assert(!recordWorkerLaunch(folder, pending, error));
    assert(error.find("previous start failed") != std::string::npos);
    assert(unlink(pending.c_str()) == 0);
    assert(recordWorkerLaunch(folder, pending, error));
    assert(unlink(pending.c_str()) == 0);
    assert(rmdir(folder) == 0);
    assert(!recordWorkerLaunch(folder, pending, error));
}
