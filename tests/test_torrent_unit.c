#define _DARWIN_C_SOURCE
#define PORTAL_HOST
#define PORTAL_DESCRIPTOR_STAT
#include "../src/torrent.c"
#include <assert.h>
static unsigned char* sample_swarm(void* payload,const char* url,size_t* size){
    assert(strstr(url,"http://tracker.opentrackr.org:1337/scrape?info_hash=")==url);
    const uint8_t* hash=payload;
    const char* fields="d8:completei12e10:incompletei3eeee";
    *size=12+20+strlen(fields);
    unsigned char* out=malloc(*size);
    memcpy(out,"d5:filesd20:",12);
    memcpy(out+12,hash,20);
    memcpy(out+32,fields,strlen(fields));
    return out;
}
int main(int argc,char**argv){
    torrent_session session={0};session_bind(&session);
    if(argc>1){FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t*data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);const uint8_t*p=data;B*root=bparse(&p,data+n,0);assert(root&&p==data+n);B*info=bget(root,"info"),*files=bget(info,"files"),*pieces=bget(info,"pieces"),*pl=bget(info,"piece length");assert(info&&pieces&&pieces->t=='s'&&pieces->n%20==0&&pl&&pl->i>0&&pl->i<=32*1024*1024);uint64_t size=0;if(files){assert(files->t=='l');for(size_t i=0;i<files->cnt;i++){B*len=bget(files->v[i],"length");assert(len&&len->t=='i'&&len->i>=0);size+=len->i;}}else{B*len=bget(info,"length");assert(len);size=len->i;}assert((size+pl->i-1)/pl->i==pieces->n/20);printf("Live portal metadata parsed: %llu bytes, %zu hashes; no payload transfer.\n",(unsigned long long)size,pieces->n/20);bfree(root);free(data);}
    const char*bad[]={"i","i12","i-0e","i01e","i9223372036854775808e","i--1e","i1xe","9:abc","999999999999999999999999999999999:ab","d1:ai1e","d1:ai1e1:ai2ee","di1ei2ee","l","01:a","d1:bi1e1:ai2ee"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++){const uint8_t*p=(const uint8_t*)bad[i];B*b=bparse(&p,p+strlen(bad[i]),0);assert(!b);}
    const char*good="d1:ai1e1:bl3:xyzee";const uint8_t*p=(const uint8_t*)good;B*b=bparse(&p,p+strlen(good),0);assert(b&&p==(const uint8_t*)good+strlen(good));assert(bget(b,"a")->i==1);bfree(b);
    uint8_t h[20];sha1((const uint8_t*)"abc",3,h);const uint8_t expected[]={0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};assert(!memcmp(h,expected,20));
    uint8_t bytes[256];srand(27);for(int i=0;i<20000;i++){size_t n=rand()%sizeof bytes;for(size_t j=0;j<n;j++)bytes[j]=rand()%256;const uint8_t*p=bytes;B*b=bparse(&p,bytes+n,0);bfree(b);}
    B name={0};name.t='s';name.s=(const uint8_t*)"..";name.n=2;char out[256];assert(component(out,sizeof out,&name)<0);name.s=(const uint8_t*)"valid.pkg";name.n=9;assert(component(out,sizeof out,&name)==0);
    char directory[]="/tmp/nullstore-mkdir-XXXXXX";assert(mkdtemp(directory));char resolved[PATH_MAX];assert(realpath(directory,resolved));char nested[512];snprintf(nested,sizeof nested,"%s/a/b",resolved);assert(mkpath(nested)==0&&mkpath(nested)==0);char link[512];snprintf(link,sizeof link,"%s/link",resolved);assert(symlink(nested,link)==0);assert(mkpath(link)<0);char file[512];snprintf(file,sizeof file,"%s/file",resolved);FILE*f=fopen(file,"w");assert(f);fclose(f);assert(mkpath(file)<0&&strstr(last_error,"not a directory"));char hash[41];const char*identity="d4:infod6:lengthi1e4:name1:x12:piece lengthi1e6:pieces20:aaaaaaaaaaaaaaaaaaaaee";assert(torrent_identity((const unsigned char*)identity,strlen(identity),hash)==0&&strlen(hash)==40);
    assert(torrent_metadata_size((const unsigned char*)identity,strlen(identity))==1);
    const char*multi="d4:infod5:filesld6:lengthi1eed6:lengthi2eee12:piece lengthi4e6:pieces20:aaaaaaaaaaaaaaaaaaaaee";
    assert(torrent_metadata_size((const unsigned char*)multi,strlen(multi))==3);
    assert(torrent_metadata_size((const unsigned char*)multi,strlen(multi)-1)==0);
    const char*inconsistent="d4:infod6:lengthi4e12:piece lengthi1e6:pieces20:aaaaaaaaaaaaaaaaaaaaee";
    assert(torrent_metadata_size((const unsigned char*)inconsistent,strlen(inconsistent))==0);
    const char* announce="http://tracker.opentrackr.org:1337/announce";
    char preview[256];
    snprintf(preview,sizeof preview,"d8:announce%zu:%s%s",strlen(announce),announce,identity+1);
    const char* previewInfo=strstr(identity,"4:info")+6;
    unsigned char previewHash[20];
    sha1((const uint8_t*)previewInfo,strlen(previewInfo)-1,previewHash);
    int seeds=-1,leeches=-1;
    assert(torrent_swarm_preview((const uint8_t*)preview,strlen(preview),sample_swarm,previewHash,&seeds,&leeches));
    assert(seeds==12&&leeches==3);
    struct sockaddr_in address;portalAddress4(&address,htonl(INADDR_LOOPBACK),6881);assert(address.sin_family==AF_INET&&ntohs(address.sin_port)==6881);
#ifdef __APPLE__
    assert(address.sin_len==sizeof address);
#endif
    uint8_t wire[]={0xff,0xff,0xff,0xff};assert(be32(wire)==UINT32_MAX);puts("Torrent parser, 20000 malformed inputs, SHA1 and path checks passed.");
}
