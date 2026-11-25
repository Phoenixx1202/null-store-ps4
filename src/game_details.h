#pragma once
#include "catalog_groups.h"

// Values in catalogue pages are untrusted. Keep foreign-language prose out of
// the UI and translate only the fixed vocabulary we can identify reliably.
inline bool hasCyrillic(const std::string& text) {
    for(size_t i=0;i<text.size();){
        unsigned ch=(unsigned char)text[i++];
        unsigned length=ch<128?0:ch<224?1:ch<240?2:3;
        if(!length)continue;
        ch&=(1u<<(6-length))-1;
        if(i+length>text.size())return true;
        while(length--)ch=(ch<<6)|((unsigned char)text[i++]&63);
        if((ch>=0x400&&ch<=0x52f)||(ch>=0x2de0&&ch<=0x2dff)||
           (ch>=0xa640&&ch<=0xa69f)||(ch>=0x1c80&&ch<=0x1c8f))return true;
    }
    return false;
}
inline std::string englishText(const std::string& text,const std::string& fallback={}) {
    return hasCyrillic(text)?fallback:text;
}
inline std::string displayGameTitle(const std::string& title) {
    auto clean=englishText(title);
    if(!clean.empty())return clean;
    // A translated title must come from a verified match. Preserve a product
    // identifier when available so two unlocalized titles can be told apart.
    auto id=listingTitle(title).productId;
    for(char& ch:id)ch=toupper((unsigned char)ch);
    return id.empty()?"Untitled PS4 listing":"PS4 listing "+id;
}
struct ListingDetails {
    std::string titleId,version,firmware,region,genres,developer,publisher,company,screen,audio,size,year;
    int seeds=-1,leeches=-1;
    bool empty() const {
        return titleId.empty()&&version.empty()&&firmware.empty()&&region.empty()&&genres.empty()&&
               developer.empty()&&publisher.empty()&&company.empty()&&screen.empty()&&audio.empty()&&
               size.empty()&&year.empty()&&seeds<0&&leeches<0;
    }
};
inline std::string detailField(const std::string& html,const std::string& label) {
    auto start=html.find(label);
    if(start==std::string::npos)return {};
    start+=label.size();
    auto end=html.find_first_of("\r\n",start);
    for(const char* term:{"<br","</li","</div","</p","</font"}){
        auto boundary=html.find(term,start);
        if(boundary<end)end=boundary;
    }
    auto value=plain(html.substr(start,std::min<size_t>(350,end-start)));
    size_t first=value.find_first_not_of(" :");
    return first==std::string::npos?std::string():value.substr(first);
}
inline std::string detailFieldAny(const std::string& html,std::initializer_list<const char*> labels) {
    for(auto* label:labels){
        auto value=detailField(html,label);
        if(!value.empty())return value;
    }
    return {};
}
inline std::string metadataLower(std::string text){
    text=lower(text);
    for(size_t i=0;i+1<text.size();++i){
        auto first=(unsigned char)text[i],second=(unsigned char)text[i+1];
        if(first!=0xd0)continue;
        if(second>=0x90&&second<=0x9f)text[i+1]=char(second+0x20);
        else if(second>=0xa0&&second<=0xaf){text[i]=char(0xd1);text[i+1]=char(second-0x20);}
        else if(second==0x81){text[i]=char(0xd1);text[i+1]=char(0x91);}
    }
    return text;
}
inline std::string mappedWords(std::string input,
                              std::initializer_list<std::pair<const char*,const char*>> words,
                              bool keepEnglish=false) {
    std::replace(input.begin(),input.end(),'/',',');
    std::set<std::string> seen;
    std::string result;
    for(size_t pos=0;pos<input.size();){
        auto end=input.find(',',pos);
        auto token=plain(input.substr(pos,end-pos));
        auto key=metadataLower(token);
        std::string english;
        for(const auto& pair:words)if(key==pair.first){english=pair.second;break;}
        if(english.empty()&&keepEnglish&&token!="-"&&token.size()<45)english=englishText(token);
        if(!english.empty()&&seen.insert(english).second){
            if(!result.empty())result+=", ";
            result+=english;
        }
        if(end==std::string::npos)break;
        pos=end+1;
    }
    return result;
}
inline std::string languages(const std::string& value) {
    auto label=mappedWords(value,{{"eng","English"},{"english","English"},{"английский","English"},
        {"rus","Russian"},{"russian","Russian"},{"русский","Russian"},{"jpn","Japanese"},
        {"japanese","Japanese"},{"японский","Japanese"},{"multi","Multiple languages"},
        {"французский","French"},{"немецкий","German"},{"испанский","Spanish"},
        {"итальянский","Italian"},{"португальский","Portuguese"},{"польский","Polish"}},true);
    if(label=="Russian, Multiple languages"||label=="Multiple languages, Russian")
        return "Multiple languages";
    return label;
}
inline std::string numericField(const std::string& value,const std::string& extras) {
    return !value.empty()&&value.size()<=16&&value.find_first_of("0123456789")!=std::string::npos&&
           value.find_first_not_of("0123456789"+extras)==std::string::npos?value:std::string();
}
inline int swarmCount(const std::string& value) {
    if(value.empty()||value.size()>9||value.find_first_not_of("0123456789")!=std::string::npos)return -1;
    return atoi(value.c_str());
}
inline ListingDetails parseListingDetails(const std::string& html,int source) {
    ListingDetails d;
    if(source<1)return d;
    // Match only field labels followed by markup so a commentary or SEO tag
    // cannot accidentally populate structured fields.
    const auto field=[&](std::initializer_list<const char*> labels){return detailFieldAny(html,labels);};
    const auto product=field({"Код диска:</b>","Код диска:"});
    if(product.size()>=9&&product.compare(0,4,"CUSA")==0&&
       product.substr(4,5).find_first_not_of("0123456789")==std::string::npos)d.titleId=product.substr(0,9);
    d.version=numericField(field({"Версия игры</b>:","Версия игры:</b>","Версия игры:"}),".");
    d.firmware=numericField(field({"Минимальная версия прошивки:</b>","Минимальная версия прошивки:"}),". +");
    const auto region=englishText(field({"Регион игры:</b>","Регион игры:"}));
    if(region=="EUR"||region=="USA"||region=="JPN"||region=="ASIA"||region=="GLOBAL")d.region=region;
    d.genres=mappedWords(field({"Жанр:</span>","Жанр: </b>","Жанр:</b>"}),
        {{"экшен","Action"},{"приключения","Adventure"},{"приключение","Adventure"},{"хоррор","Horror"},
        {"ужасы","Horror"},{"ролевые","RPG"},{"рпг","RPG"},{"пошаговые","Turn-based"},{"гонки","Racing"},
        {"спортивные","Sports"},{"симуляторы","Simulation"},{"стратегии","Strategy"},
        {"головоломки","Puzzle"},{"платформеры","Platformer"},{"драки","Fighting"},
        {"шутер","Shooter"},{"выживание","Survival"},{"инди","Indie"}},true);
    d.developer=englishText(field({"Разработчик:</span>","Разработчик:</b>"}));
    d.publisher=englishText(field({"Издатель:</span>","Издатель:</b>","Издательство:</b>"}));
    d.company=englishText(field({"Разработчик / Издатель:</span>"}));
    d.screen=languages(field({"Язык интерфейса:</span>","Интерфейс:</span>","Язык интерфейса игры:</b>","Язык интерфейса:</b>"}));
    d.audio=languages(field({"Язык озвучки:</span>","Озвучка:</span>","Язык озвучки</b>:","Язык озвучки:</b>"}));
    d.year=numericField(field({"Дата выхода:</span>","Год выпуска: </b>","Год выпуска:</b>"}),"");
    if(d.year.size()!=4)d.year.clear();
    auto size=englishText(field({"Размер:</span>","Размер раздачи:</b>","page__subtitle--dl\">Размер:"}));
    if(size.size()<24&&sizeMB(size)>0)d.size=size;
    d.seeds=swarmCount(field({"data-stat=\"seeders\">","Сиды:</span>","Сиды:</b>","Seeders:</span>"}));
    d.leeches=swarmCount(field({"data-stat=\"leechers\">","Личи:</span>","Личи:</b>","Leechers:</span>"}));
    return d;
}
struct EnglishOverview {std::string title,summary,genre,publisher,players;};
inline EnglishOverview englishOverview(const std::string& title) {
    // Original summaries checked against the corresponding English PS Store
    // product pages. Exact normalized matches keep editions and sequels apart.
    const auto key=listingTitle(title).key;
    if(key=="sugar rush rescue")return {"Sugar Rush Rescue",
        "Colby and Jack explore a candy forest after a giant owl snatches Colby's sister. Work together to dodge enemies and navigate traps across a retro adventure built for local co-op.",
        "Adventure","Cascadia Games LLC","1–2 players"};
    if(key=="bubsy 4d")return {"Bubsy 4D",
        "Take Bubsy across alien worlds in a 3D platforming adventure. Collect yarn, outmaneuver robotic sheep, and use glide and hairball moves to find shortcuts and improve your time trials.",
        "Action / Platformer","Atari","1 player"};
    if(key=="dying light: definitive edition"||key=="dying light definitive edition")return {"Dying Light: Definitive Edition",
        "Explore a quarantined city on foot and by rooftop, scavenging by day and surviving more dangerous infected after dark. This edition includes The Following and additional expansions and content.",
        "Action / Survival horror","Warner Bros. Interactive","1 player / Up to 4 online"};
    if(key=="scarlet nexus")return {"SCARLET NEXUS",
        "Follow Yuito or Kasane in a future where soldiers use psychic powers against creatures called the Others. Combine telekinesis with close-range attacks and borrow abilities from squadmates.",
        "Action RPG","Bandai Namco Entertainment","1 player"};
    if(key=="dragon quest xi: echoes of an elusive age")return {"Dragon Quest XI: Echoes of an Elusive Age",
        "Travel across Erdrea as the Luminary, a young hero hunted for the power he carries. Recruit companions, explore towns and dungeons, and take on monsters in turn-based battles.",
        "Role-playing / Adventure","Square Enix",""};
    return {};
}
