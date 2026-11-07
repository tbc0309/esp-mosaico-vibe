#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace bridge { namespace update_policy {
inline bool version(const std::string &text,std::array<unsigned,3> &out){
    size_t at=0;
    for(unsigned i=0;i<3;++i){
        if(at>=text.size()||text[at]<'0'||text[at]>'9')return false;
        unsigned number=0;
        do{if(number>99999)return false;number=number*10+unsigned(text[at++]-'0');}
        while(at<text.size()&&text[at]>='0'&&text[at]<='9');
        out[i]=number;
        if(i<2&&(at>=text.size()||text[at++]!='.'))return false;
    }
    return at==text.size();
}
inline bool newer(const std::string &candidate,const std::string &current){
    std::array<unsigned,3> a{},b{};return version(candidate,a)&&version(current,b)&&a>b;
}
inline bool checksum(const std::string &value){
    if(value.size()!=64)return false;
    for(char c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
inline bool redirect(const std::string &url){
    if(url.rfind("https://",0)!=0)return false;
    std::string host=url.substr(8,url.find('/',8)-8);
    return host=="github.com"||(host.size()>22&&host.compare(host.size()-22,22,".githubusercontent.com")==0);
}
inline bool field(const uint8_t *value,const char *expected){
    size_t length=strlen(expected);return length<32&&!memcmp(value,expected,length)&&value[length]==0;
}
inline bool header(const uint8_t *data,size_t size,unsigned chip,const char *project,const char *expectedVersion=""){
    return size>=288&&data[0]==0xe9&&(unsigned(data[12])|(unsigned(data[13])<<8))==chip&&
        data[32]==0x32&&data[33]==0x54&&data[34]==0xcd&&data[35]==0xab&&
        field(data+80,project)&&(!*expectedVersion||field(data+48,expectedVersion));
}
}}
