#pragma once
#include "install_log.h"
#include <algorithm>
#include <string>
#ifndef PORTAL_HOST
#include <orbis/libkernel.h>
#else
#include <orbis/_types/kernel.h>
extern "C" int32_t sceKernelGetModuleList(uint32_t*,size_t,size_t*);
extern "C" int32_t sceKernelGetModuleInfo(uint32_t,OrbisKernelModuleInfo*);
#endif
static int loadedModule(const char*path){
    static_assert(sizeof(OrbisKernelModuleInfo)==352,"PS4 module-info ABI");
    uint32_t handles[256]={};size_t count=0;int r=sceKernelGetModuleList(handles,256,&count);
    if(r){installLog("Module inventory failed: 0x%08X",(unsigned)r);return -1;}
    std::string target=path;auto slash=target.find_last_of('/');if(slash!=std::string::npos)target=target.substr(slash+1);auto dot=target.find('.');if(dot!=std::string::npos)target.resize(dot);
    for(size_t i=0;i<std::min(count,size_t(256));i++){
        OrbisKernelModuleInfo info={};info.size=sizeof info;if(sceKernelGetModuleInfo(handles[i],&info))continue;
        info.name[sizeof info.name-1]=0;std::string name=info.name;
        auto end=name.find_last_of('/');if(end!=std::string::npos)name=name.substr(end+1);end=name.find('.');if(end!=std::string::npos)name.resize(end);
        if(name==target){installLog("Reusing mapped module %s: handle=0x%08X",target.c_str(),handles[i]);return (int)handles[i];}
    }return -1;
}
