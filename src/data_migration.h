#pragma once
#include "data_root.h"
#include "queue_store.h"
#include <sys/file.h>

inline bool hasPendingStoreInstall(const std::string& directory) {
    std::string error;
    const auto records=loadQueue(directory+"/installs.tsv",nullptr,&error);
    if(!error.empty())return true;
    for(size_t start=0;start<records.size();){
        auto end=records.find('\n',start);
        const auto row=records.substr(start,end-start);
        if(end==std::string::npos)end=records.size();
        start=end+1;
        size_t task=0;
        for(int field=0;field<5&&task<row.size();++field){
            auto tab=row.find('\t',task);
            task=tab==std::string::npos?row.size():tab+1;
        }
        auto stateField=row.find('\t',task);
        if(stateField==std::string::npos)continue;
        const int state=atoi(row.c_str()+stateField+1);
        if(state==1||state==2||state==3||(state==6&&atoi(row.c_str()+task)>=0))return true;
    }
    return false;
}

inline bool migrateStoreData(const std::string& legacy,const std::string& current) {
    struct stat info={};
    if(portalPathInfo(current.c_str(),&info)==0||errno!=ENOENT)return false;
    if(portalPathInfo(legacy.c_str(),&info)!=0||!S_ISDIR(info.st_mode))return false;

    const auto lockPath=legacy+"/queue-owner.lock";
    int fd=open(lockPath.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0666);
    if(fd<0)return false;
    const bool locked=portalDescriptorInfo(fd,&info)==0&&S_ISREG(info.st_mode)&&flock(fd,LOCK_EX|LOCK_NB)==0;
    bool migrated=false;
    if(locked&&!hasPendingStoreInstall(legacy)&&portalPathInfo(current.c_str(),&info)!=0&&errno==ENOENT){
        // Mark the move inside the directory so the marker and all existing
        // queue files land at the new path in a single rename.
        if(durableReplace(legacy+"/.migrated-from-portal-store","1\n",0644))
            migrated=rename(legacy.c_str(),current.c_str())==0;
    }
    close(fd);
    return migrated;
}
