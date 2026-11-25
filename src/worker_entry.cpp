#include "worker_entry.h"
#include <cstring>
#include <orbis/SystemService.h>

int main(int argc, char** argv) {
    // LaunchApp may supply the executable name before the caller's argument.
    const char* argument = argv && (argc == 1 || argc == 2) ? argv[argc - 1] : nullptr;
    const int result = argument && std::strcmp(argument, kWorkerStartArgument) == 0 ?
        runPackagedWorker() : 0;
    sceSystemServiceLoadExec("exit", nullptr);
    return result;
}
