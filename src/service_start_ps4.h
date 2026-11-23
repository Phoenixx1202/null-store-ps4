#pragma once
#include <string>
bool portalServicePrivileges(std::string&);
bool portalServiceLaunch(std::string&);
bool portalServiceInstallAndLaunch(std::string&);
bool portalWorkerRunning();
bool portalStartPackagedWorker(std::string& error);
bool portalWorkerBuildCompatible(const std::string& build);
