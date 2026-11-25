#pragma once
#include "data_root.h"
#include <string>

inline std::string defaultDownloadDir(const std::string&root,int id){return root+"/downloads/"+std::to_string(id);}
inline bool safeDownloadPathParts(const std::string& path){
    if(path.empty()||path[0]!='/'||path.size()>700)return false;
    for(unsigned char ch:path)if(ch<32||ch==127||ch=='\\')return false;
    for(size_t start=1;start<=path.size();){
        auto end=path.find('/',start);
        auto part=path.substr(start,end-start);
        if(part.empty()||part=="."||part=="..")return false;
        if(end==std::string::npos)break;
        start=end+1;
    }
    return true;
}
inline std::string migratedDownloadPath(const std::string& root,const std::string& path){
    const std::string old=NULL_STORE_LEGACY_DATA_ROOT;
    if(root==NULL_STORE_DATA_ROOT&&(path==old||path.compare(0,old.size()+1,old+"/")==0)){
        struct stat info={};
        if(portalPathInfo((root+"/.migrated-from-portal-store").c_str(),&info)==0&&S_ISREG(info.st_mode))
            return root+path.substr(old.size());
    }
    return path;
}
inline std::string downloadVolume(const std::string&root,const std::string&path){
    if(path==root||path.compare(0,root.size()+1,root+"/")==0)return root;
    if(root.compare(0,6,"/data/")==0&&(path=="/data"||path.compare(0,6,"/data/")==0))return root;
    for(int i=0;i<8;i++){
        auto volume="/mnt/usb"+std::to_string(i);
        if(path==volume||path.compare(0,volume.size()+1,volume+"/")==0)return volume;
    }
    return {};
}
inline bool mountedUsbVolume(const std::string& volume){
    struct stat mountPoint={},parent={};
    if(portalPathInfo(volume.c_str(),&mountPoint)!=0||!S_ISDIR(mountPoint.st_mode))return false;
    return portalPathInfo("/mnt",&parent)==0&&mountPoint.st_dev!=parent.st_dev;
}
inline bool validDownloadBase(const std::string&root,const std::string&base){
    return safeDownloadPathParts(base)&&!downloadVolume(root,base).empty();
}
inline bool validDownloadDir(const std::string&root,int id,const std::string&dir){
    if(id<=0||!safeDownloadPathParts(dir))return false;
    const auto suffix="/downloads/"+std::to_string(id);
    if(dir.size()<=suffix.size()||dir.compare(dir.size()-suffix.size(),suffix.size(),suffix))return false;
    return validDownloadBase(root,dir.substr(0,dir.size()-suffix.size()));
}
// Inspect each component individually so a renamed folder or symlink cannot
// redirect an install or deletion outside the selected volume.
inline bool downloadDirectoryChain(const std::string& path,bool allowMissingLeaf=false){
    if(!safeDownloadPathParts(path)){errno=EINVAL;return false;}
    size_t first=1;
#if defined(PORTAL_HOST) && defined(__APPLE__)
    if(path.compare(0,5,"/tmp/")==0)first=5;
    if(path.compare(0,5,"/var/")==0)first=5;
#endif
    for(size_t end=path.find('/',first);;end=path.find('/',end+1)){
        auto prefix=path.substr(0,end);
        struct stat info={};
        if(portalPathInfo(prefix.c_str(),&info)){
            if(allowMissingLeaf&&end==std::string::npos&&errno==ENOENT)return true;
            return false;
        }
        if(!S_ISDIR(info.st_mode)){errno=ENOTDIR;return false;}
        if(end==std::string::npos)return true;
    }
}
inline std::string downloadJobDirFromFile(const std::string& root,int id,const std::string& path){
    const auto marker="/downloads/"+std::to_string(id)+"/";
    for(size_t next=0;(next=path.find(marker,next))!=std::string::npos;next+=marker.size()){
        auto dir=path.substr(0,next+marker.size()-1);
        if(validDownloadDir(root,id,dir)&&safeDownloadPathParts(path))return dir;
    }
    return {};
}
inline bool pathInsideDownloadDir(const std::string&root,int id,const std::string&dir,const std::string&path){
    return validDownloadDir(root,id,dir)&&safeDownloadPathParts(path)&&
           path.size()>dir.size()+1&&path.compare(0,dir.size()+1,dir+"/")==0;
}
