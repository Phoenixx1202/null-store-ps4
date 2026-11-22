#pragma once
#include <string>
#include <cstdint>
class NativeTextInput {
    bool ready=false,opened=false;
    unsigned openingFrames=0;
    alignas(4) char16_t buffer[129]={};
public:
    enum Result {Pending,Accepted,Cancelled,Failed};
    ~NativeTextInput();
    bool isOpen()const{return opened;}
    // Opens the console's keyboard; `search` labels its button Search, else OK.
    bool begin(int user,const std::string&initial,std::string&error,const char16_t*title=u"Search games",const char16_t*placeholder=u"Game title",bool search=true);
    Result poll(std::string&text,std::string&error);
};
