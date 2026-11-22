#include "service_protocol.h"
#include "service_setup_log.h"
#include "service_start_ps4.h"
#include <orbis/SystemService.h>
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
    serviceSetupLog(true, "Packaged worker startup: NULL00003 build=%s", expected.c_str());
    if (!portalWorkerRunning()) {
        LncAppParam params = {};
        params.size = sizeof params;
        params.user_id = static_cast<uint32_t>(-1);
        params.LaunchAppCheck_flag = LaunchApp_None;
        const int result = sceLncUtilLaunchApp("NULL00003", nullptr, &params);
        serviceSetupLog(false, "Packaged worker launch => 0x%08X", static_cast<unsigned>(result));
        if (result < 0 && !portalWorkerRunning()) {
            char code[16];
            snprintf(code, sizeof code, "0x%08X", static_cast<unsigned>(result));
            error = std::string("PS4 could not launch Download Worker (") + code +
                    "). Choose Install included Download Worker in Downloads.";
            return false;
        }
        for (int i = 0; i < 150 && !portalWorkerRunning(); ++i) sceKernelUsleep(100000);
    }
    if (!portalWorkerRunning()) error = "Download Worker did not answer. Check service.log for startup details.";
    else if (!portalWorkerBuildCompatible(runningBuild()))
        error = "A different worker is running. Finish its downloads before switching builds.";
    serviceSetupLog(false, "Packaged worker result: %s", error.empty() ? "ready" : error.c_str());
    return error.empty();
}
