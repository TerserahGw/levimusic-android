#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace lm_offline {
inline std::string extension(std::string_view bytes,std::string mime){
    auto starts=[&](std::string_view signature){return bytes.substr(0,signature.size())==signature;};
    if(starts("OggS"))return ".ogg";
    if(starts("fLaC"))return ".flac";
    if(starts("RIFF")&&bytes.size()>=12&&bytes.substr(8,4)=="WAVE")return ".wav";
    if(bytes.size()>=4&&static_cast<unsigned char>(bytes[0])==0x1a&&
       static_cast<unsigned char>(bytes[1])==0x45&&
       static_cast<unsigned char>(bytes[2])==0xdf&&
       static_cast<unsigned char>(bytes[3])==0xa3)return ".webm";
    if(bytes.size()>=8&&bytes.substr(4,4)=="ftyp")return ".m4a";
    if(starts("ID3"))return ".mp3";
    if(bytes.size()>=2&&static_cast<unsigned char>(bytes[0])==0xff){
        auto second=static_cast<unsigned char>(bytes[1]);
        if((second&0xf6)==0xf0)return ".aac";
        if((second&0xe0)==0xe0)return ".mp3";
    }
    if(!bytes.empty()&&(bytes[0]=='<'||bytes[0]=='{'||bytes[0]=='['))return {};
    // Only trust an audio MIME type when the server omits a recognizable header.
    std::transform(mime.begin(),mime.end(),mime.begin(),[](unsigned char ch){return std::tolower(ch);});
    if(mime.rfind("audio/",0)!=0)return {};
    if(mime.find("webm")!=std::string::npos)return ".webm";
    if(mime.find("mpeg")!=std::string::npos)return ".mp3";
    if(mime.find("ogg")!=std::string::npos)return ".ogg";
    if(mime.find("aac")!=std::string::npos)return ".aac";
    if(mime.find("flac")!=std::string::npos)return ".flac";
    if(mime.find("wav")!=std::string::npos)return ".wav";
    if(mime.find("mp4")!=std::string::npos||mime.find("m4a")!=std::string::npos)return ".m4a";
    return {};
}
}
