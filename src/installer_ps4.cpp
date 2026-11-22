#include "installer.h"
#include "mapped_module.h"
#include "install_log.h"
#include "bgft_abi.h"
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/AppInstUtil.h>
#ifndef PORTAL_HOST
#include <orbis/libkernel.h>
#else
#include <orbis/_types/kernel.h>
extern "C" int32_t sceKernelGetModuleList(uint32_t*,size_t,size_t*);
extern "C" int32_t sceKernelGetModuleInfo(uint32_t,OrbisKernelModuleInfo*);
extern "C" uint32_t sceKernelLoadStartModule(const char*,size_t,const void*,uint32_t,void*,void*);
extern "C" const char* sceKernelGetFsSandboxRandomWord();
extern "C" int32_t sceKernelDlsym(int32_t,const char*,void**);
#endif
#include <cstdlib>
#include <cstring>
#include <map>
// PS4 Games Installer uses a public BGFT session with StorageEx for local PKGs.
// Keep task start, resume and progress on that same session.
using NullBGFT::Init;using NullBGFT::ParamsEx;using NullBGFT::Progress;
extern "C" int bgftInit(Init*) __asm__("sceBgftServiceInit");
extern "C" int bgftTerm() __asm__("sceBgftServiceTerm");
extern "C" int bgftRegister(ParamsEx*,int*) __asm__("sceBgftServiceIntDownloadRegisterTaskByStorageEx");
extern "C" int bgftStart(int) __asm__("sceBgftServiceDownloadStartTask");
extern "C" int bgftResume(int) __asm__("sceBgftServiceDownloadResumeTask");
extern "C" int bgftProgress(int,Progress*) __asm__("sceBgftServiceDownloadGetProgress");
static bool bgftReady=false,moduleReady=false,cleanupRegistered=false;static void*heap=nullptr;
static std::map<int,int>progressReports;
static std::string apiError(const char*what,int code){installLog("%s: 0x%08X",what,(unsigned)code);char s[240];snprintf(s,sizeof s,"%s (0x%08X). See installer.log for details.",what,(unsigned)code);return s;}
static void finalize(){
    if(bgftReady){int r=bgftTerm();installLog("sceBgftServiceTerm: 0x%08X",(unsigned)r);bgftReady=false;}
    free(heap);heap=nullptr;
}
static bool loadNativeModule(const char*name,const char*path,std::string&error){
    // Match flatz/module.c: titles see system libraries through a randomized
    // mount. /system/common/lib is a path outside the normal title namespace.
    const char*word=sceKernelGetFsSandboxRandomWord();
    if(word&&*word){
        const char*file=strrchr(path,'/');file=file?file+1:path;
        std::string sandboxPath="/"+std::string(word)+"/common/lib/"+file;
        int entryResult=0;int handle=(int)sceKernelLoadStartModule(sandboxPath.c_str(),0,0,0,0,&entryResult);
        installLog("Installer module %s: sandbox path=%s handle=0x%08X start=0x%08X",name,sandboxPath.c_str(),(unsigned)handle,(unsigned)entryResult);
        if(handle>=0&&entryResult>=0)return true;
        int code=handle<0?handle:entryResult;
        // Only missing paths fall through to the global namespace. Preserve a
        // real loader/start error instead of hiding it behind another ENOENT.
        if((uint32_t)code!=0x80020002u&&(uint32_t)code!=0x80020014u){
            std::string stage="Could not load installer module ";stage+=name;
            error=apiError(stage.c_str(),code);return false;
        }
    }else installLog("Installer module %s: no sandbox mount reported; trying global path",name);
    // The global path remains useful when running outside a title sandbox.
    int entryResult=0;int handle=(int)sceKernelLoadStartModule(path,0,0,0,0,&entryResult);
    installLog("Installer module %s: native path=%s handle=0x%08X start=0x%08X",name,path,(unsigned)handle,(unsigned)entryResult);
    if(handle>=0&&entryResult>=0)return true;
    std::string stage="Could not load installer module ";stage+=name;
    error=apiError(stage.c_str(),handle<0?handle:entryResult);return false;
}
static bool loadModule(OrbisSysModuleInternal id,const char*name,const char*path,std::string&error){
    if(loadedModule(path)>=0)return true;
    int r=(int)sceSysmoduleLoadModuleInternal(id);installLog("Installer module %s id=0x%08X: sysmodule=0x%08X",name,(unsigned)id,(unsigned)r);
    if(!r)return true;return loadNativeModule(name,path,error);
}
static bool initialize(std::string&error){
    if(bgftReady)return true;
    installLog("NULL_store 1.1 local PKG installer: opening public BGFT session (module ready=%d, SELF installer auth configured)",moduleReady);
    if(!moduleReady){
        if(!loadModule(ORBIS_SYSMODULE_INTERNAL_BGFT,"BGFT","/system/common/lib/libSceBgft.sprx",error))return false;
        moduleReady=true;
    }
    if(!cleanupRegistered){if(atexit(finalize)){error="Could not register installer cleanup";return false;}cleanupRegistered=true;}
    const size_t bytes=1024*1024;
    heap=calloc(1,bytes);if(!heap){error="Cannot allocate installer memory";return false;}
    Init params={heap,bytes};int r=bgftInit(&params);installLog("sceBgftServiceInit: heap=%zu params=%zu result=0x%08X",bytes,sizeof params,(unsigned)r);
    if(r){free(heap);heap=nullptr;error=apiError("BGFT service initialization failed",r);return false;}
    bgftReady=true;return true;
}
static bool registerTask(const InstallPackage&pkg,int&id,std::string&error){
    id=-1;const char*type=NullBGFT::packageType(pkg.contentType);
    if(!type){error="Unsupported PS4 package type";return false;}
    if(!initialize(error))return false;
    if(pkg.contentId.size()!=36||pkg.titleId.size()!=9||pkg.contentId.substr(7,9)!=pkg.titleId){error="Package identity does not match its verified PKG header";return false;}
    const char*title=pkg.titleId.c_str();
    installLog("PKG identity from validated header: title=%s content=%s type=0x%X",title,pkg.contentId.c_str(),pkg.contentType);
    int user=-1;int r=sceUserServiceGetForegroundUser(&user);
    installLog("Foreground installer user: result=0x%08X user=%d",(unsigned)r,user);
    if(r||user<0){user=-1;r=sceUserServiceGetInitialUser(&user);installLog("Initial-user fallback: result=0x%08X user=%d",(unsigned)r,user);}
    if(r){error=apiError("Cannot determine installer user",r);return false;}
    if(user<0){error="Sign in to a PS4 user before installing.";return false;}
    std::string path=storageInstallPath(pkg.path);ParamsEx params={};
    params.params.userId=user;params.params.entitlementType=5;params.params.id=pkg.contentId.c_str();params.params.url=path.c_str();params.params.name=pkg.name.c_str();params.params.icon="";params.params.scenario="0";
    params.params.option=NullBGFT::DisableCDNQuery;params.params.type=type;params.params.subtype="";params.params.size=pkg.bytes;params.slot=0;
    installLog("StorageEx register: %s content=%s type=%s flags=0x%08X patch=%d size=%llu user=%d option=0x%X",path.c_str(),pkg.contentId.c_str(),type,pkg.contentFlags,pkg.patch,(unsigned long long)pkg.bytes,user,params.params.option);
    r=bgftRegister(&params,&id);installLog("sceBgftServiceIntDownloadRegisterTaskByStorageEx: result=0x%08X task=%d",(unsigned)r,id);
    if(r){id=-1;if((uint32_t)r==0x80990088u)error=apiError("This package is already installed",r);else if((uint32_t)r==0x80990015u||(uint32_t)r==0x80990086u)error=apiError("A system installation task already exists",r);else error=apiError("Could not register installation",r);return false;}
    if(id<0){error="PS4 returned no installation task ID.";return false;}return true;
}
static bool startTask(int id,std::string&error){
    if(id<0){error="Invalid installation task ID";return false;}if(!initialize(error))return false;
    int r=bgftStart(id);installLog("sceBgftServiceDownloadStartTask: task=%d result=0x%08X",id,(unsigned)r);if(r){error=apiError("Could not start system installation",r);return false;}return true;
}
static bool resumeTask(int id,std::string&error){
    if(id<0){error="Invalid installation task ID";return false;}if(!initialize(error))return false;
    int r=bgftResume(id);installLog("sceBgftServiceDownloadResumeTask: task=%d result=0x%08X",id,(unsigned)r);
    // A registered task that has not started yet needs Start rather than Resume.
    if(r)return startTask(id,error);return true;
}
static bool pollTask(int id,int&percent,bool&complete,std::string&error){
    complete=false;if(id<0){error="Invalid installation task ID";return false;}if(!initialize(error))return false;
    Progress p={};int r=bgftProgress(id,&p);
    if(r||p.error){installLog("BGFT progress failed: task=%d result=0x%08X error=0x%08X bits=0x%X",id,(unsigned)r,(unsigned)p.error,p.bits);error=apiError("System installation needs attention",r?r:p.error);return false;}
    const uint64_t length=p.lengthTotal?p.lengthTotal:p.length;
    const uint64_t done=p.lengthTotal?p.transferredTotal:p.transferred;
    // StorageEx can report copy=100 before the system transfers any bytes.
    // Use the whole transfer for progress and check all three fields before
    // completing the task, as in PS4 Games Installer's taskProgress().
    complete=length>0&&done>=length&&p.preparing==100&&p.localCopy==100;
    percent=-1;
    if(complete)percent=100;
    else if(length>0&&done<length)percent=int((long double)done*100/length);
    else if(p.localCopy>0&&p.localCopy<100)percent=p.localCopy;
    int bucket=percent<0?-1:percent/5;if(!progressReports.count(id)||progressReports[id]!=bucket||complete){
        installLog("BGFT progress: task=%d bits=0x%X bytes=%llu/%llu prepare=%d copy=%d percent=%d complete=%d",id,p.bits,(unsigned long long)done,(unsigned long long)length,p.preparing,p.localCopy,percent,complete);
        if(progressReports.size()>256)progressReports.clear();progressReports[id]=bucket;
    }
    return true;
}
template<class Function>
static bool appInstSymbol(int handle, const char* name, Function& function, std::string& error) {
    void* symbol = nullptr;
    const int result = sceKernelDlsym(handle, name, &symbol);
    if (result || !symbol) {
        error = apiError(name, result);
        return false;
    }
    function = reinterpret_cast<Function>(symbol);
    return true;
}

static bool uninstallDownloadWorker(std::string& error) {
    constexpr const char* title = "NULL00003";
    constexpr const char* content = "IV0000-NULL00003_00-NULLSTOREWORKER0";
    int handle = loadedModule("libSceAppInstUtil");
    if (handle < 0) {
        const int result = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);
        if (result) { error = apiError("Worker uninstall module unavailable", result); return false; }
        handle = loadedModule("libSceAppInstUtil");
    }
    if (handle < 0) { error = "PS4 could not open the worker uninstall module."; return false; }

    decltype(&sceAppInstUtilInitialize) init;
    decltype(&sceAppInstUtilTerminate) term;
    decltype(&sceAppInstUtilAppExists) exists;
    decltype(&sceAppInstUtilAppIsInInstalling) installing;
    decltype(&sceAppInstUtilAppUnInstall) remove;
    if (!appInstSymbol(handle, "sceAppInstUtilInitialize", init, error) ||
        !appInstSymbol(handle, "sceAppInstUtilTerminate", term, error) ||
        !appInstSymbol(handle, "sceAppInstUtilAppExists", exists, error) ||
        !appInstSymbol(handle, "sceAppInstUtilAppIsInInstalling", installing, error) ||
        !appInstSymbol(handle, "sceAppInstUtilAppUnInstall", remove, error)) return false;

    int result = init();
    if (result) { error = apiError("Could not initialize worker removal", result); return false; }
    int32_t present = 0;
    if (installing(content)) {
        error = "Wait for the worker installation to finish before removing it.";
    } else if ((result = exists(title, &present)) != 0) {
        error = apiError("Could not check the installed worker", result);
    } else if (present) {
        if ((result = remove(title)) != 0) error = apiError("PS4 could not uninstall the worker", result);
        else if ((result = exists(title, &present)) != 0) error = apiError("Could not verify worker removal", result);
        else if (present) error = "PS4 accepted removal. The worker is still listed. Check Notifications.";
    }
    term();
    installLog("Download Worker uninstall (%s): %s", title, error.empty() ? "confirmed absent" : error.c_str());
    return error.empty();
}
InstallBackend ps4InstallBackend(){return {registerTask,startTask,pollTask,resumeTask,uninstallDownloadWorker};}
