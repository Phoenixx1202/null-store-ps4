#include "service_protocol.h"
#include "service_setup_log.h"
#include "service_start_ps4.h"
#include "installer.h"
#include "worker_entry.h"
#include "worker_launch_attempt.h"
#include <orbis/SystemService.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

bool portalWorkerRunning() {
    PortalIPC::Writer w;
    w.u32(PortalIPC::PING);
    std::string reply, error;
    if (!PortalIPC::request(w.bytes, reply, error)) return false;
    try {
        PortalIPC::Reader r(reply);
        r.u32();
        r.string();
        r.u32();
        r.end();
        return true;
    } catch (...) {
        return false;
    }
}
static std::string runningBuild() {
    PortalIPC::Writer w;
    w.u32(PortalIPC::BUILD_ID);
    std::string reply, error;
    if (!PortalIPC::request(w.bytes, reply, error)) return {};
    try {
        PortalIPC::Reader r(reply);
        if (r.u32() != PortalIPC::VERSION) return {};
        auto build = r.string();
        r.end();
        return build;
    } catch (...) {
        return {};
    }
}

bool portalWorkerBuildCompatible(const std::string& build){
    static const std::string bundled=readFile("/app0/assets/worker/build-id.txt");
    return build.size()==64 && build==bundled;
}

bool portalStartPackagedWorker(std::string& error) {
    const auto expected = readFile("/app0/assets/worker/build-id.txt");
    error.clear();
    if (expected.size() != 64 || expected.find_first_not_of("0123456789abcdef") != std::string::npos) {
        error = "Missing worker build ID. Reinstall the store package.";
        return false;
    }
    serviceSetupLog(false, "Packaged worker startup: NULL00003 build=%s", expected.c_str());
    std::string attempt;
    if (!portalWorkerRunning()) {
        int32_t user = -1;
        const int userResult = sceUserServiceGetForegroundUser(&user);
        if (userResult != 0 || user <= 0) {
            error = "Sign in to a PS4 user before starting the worker.";
            serviceSetupLog(false, "Worker launch blocked: foreground user result=0x%08X", static_cast<unsigned>(userResult));
            return false;
        }
        const std::string root = nullStoreDataRoot();
        attempt = root + "/worker-launch-" + expected + ".pending";
        if (access(attempt.c_str(), F_OK) == 0) {
            error = "This worker's previous start failed or was interrupted. Keep NULL_store open to download.";
            serviceSetupLog(false, "Worker launch blocked by previous start record");
            return false;
        }
        if (!ps4WorkerInstallVerified()) {
            error = "The installed worker could not be verified. Keep NULL_store open to download.";
            serviceSetupLog(false, "Worker launch skipped: %s", error.c_str());
            return false;
        }
        if (!recordWorkerLaunch(root, attempt, error)) {
            serviceSetupLog(false, "Worker launch skipped: %s", error.c_str());
            return false;
        }
        LncAppParam params = {};
        params.size = sizeof params;
        params.user_id = static_cast<uint32_t>(user);
        params.LaunchAppCheck_flag = LaunchApp_None;
        const char* arguments[] = { kWorkerStartArgument, nullptr };
        serviceSetupLog(false, "Calling PS4 launcher for the foreground user");
        const int result = sceLncUtilLaunchApp("NULL00003", arguments, &params);
        serviceSetupLog(false, "Packaged worker launch => 0x%08X", static_cast<unsigned>(result));
        if (result < 0 && !portalWorkerRunning()) {
            char code[16];
            snprintf(code, sizeof code, "0x%08X", static_cast<unsigned>(result));
            error = std::string("PS4 could not launch Download Worker (") + code +
                    "). Keep NULL_store open to download.";
            return false;
        }
        const uint64_t deadline = sceKernelGetProcessTime() + 15000000;
        while (sceKernelGetProcessTime() < deadline) {
            if (portalWorkerRunning()) break;
            sceKernelUsleep(250000);
        }
    }
    if (!portalWorkerRunning()) error = "Download Worker did not answer. Check service.log for startup details.";
    else if (!portalWorkerBuildCompatible(runningBuild()))
        error = "A different worker is running. Finish its downloads before switching builds.";
    else if (!attempt.empty() && unlink(attempt.c_str()) != 0)
        serviceSetupLog(false, "Could not clear the completed worker start record: errno=%d", errno);
    serviceSetupLog(false, "Packaged worker result: %s", error.empty() ? "ready" : error.c_str());
    return error.empty();
}
