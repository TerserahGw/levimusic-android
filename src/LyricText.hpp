#pragma once
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace lm_lyric {
inline std::string camelSpaces(std::string text){
    if(text.find_first_of(" \t")!=std::string::npos)return text;
    std::string out;
    for(size_t i=0;i<text.size();++i){
        unsigned char c=text[i];
        if(i&&c>='A'&&c<='Z'&&text[i-1]>='a'&&text[i-1]<='z')out+=' ';
        out+=text[i];
    }
    return out;
}
inline std::string normalize(std::string_view input){
    std::string stripped;
    std::vector<std::string> pieces(1);
    bool hadMarkers=false;
    for(size_t i=0;i<input.size();){
        if(input[i]=='<'){
            size_t end=input.find('>',i+1);
            if(end!=input.npos&&end-i<=14&&input.substr(i+1,end-i-1).find(':')!=input.npos){
                hadMarkers=true;i=end+1;pieces.emplace_back();continue;
            }
        }
        stripped+=input[i];
        pieces.back()+=input[i];
        ++i;
    }
    if(hadMarkers&&stripped.find_first_of(" \t") == std::string::npos){
        std::string joined;
        bool latin=true;int count=0;
        for(const auto& piece:pieces){
            if(piece.empty())continue;
            ++count;
            for(unsigned char c:piece)if(c>=128)latin=false;
            if(!joined.empty()&&std::string(",.!?;:)]}").find(piece.front())==std::string::npos&&
               joined.back()!='-'&&joined.back()!='\'')joined+=' ';
            joined+=piece;
        }
        std::string compact;
        for(unsigned char c:joined)if(!std::isspace(c))compact+=static_cast<char>(c);
        if(latin&&count>1&&joined.size()<1000&&compact==stripped)return joined;
    }
    return camelSpaces(stripped);
}
}
