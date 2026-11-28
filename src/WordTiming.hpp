#pragma once
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lm_timing {
struct WordCue {
    int startMs=0,endMs=0;
    std::string text;
};

inline size_t revealedCount(const std::vector<WordCue>& words,int positionMs){
    return static_cast<size_t>(std::upper_bound(words.begin(),words.end(),positionMs,
        [](int position,const WordCue& word){return position<word.startMs;})-words.begin());
}

inline size_t utf8Length(std::string_view value){
    size_t count=0;
    for(unsigned char ch:value)if((ch&0xC0)!=0x80)++count;
    return count;
}

// Match provider word cues back to the original lyric without inventing
// character boundaries. Gaps are kept with the following cue, so punctuation
// such as Japanese 、。 remains visible while every returned boundary still
// belongs to an authentic provider timestamp.
inline std::vector<size_t> alignedTokenEnds(
    std::string_view line,const std::vector<WordCue>& words){
    std::vector<size_t> ends;
    if(line.empty()||words.empty())return ends;
    size_t cursor=0;
    for(const auto& word:words){
        const auto first=word.text.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)return {};
        const auto last=word.text.find_last_not_of(" \t\r\n");
        const std::string_view token(word.text.data()+first,last-first+1);
        const size_t at=line.find(token,cursor);
        if(at==std::string_view::npos)return {};
        cursor=at+token.size();
        ends.push_back(cursor);
    }
    if(cursor<line.size())ends.back()=line.size();
    return ends;
}

inline std::string revealedText(std::string_view line,
                                const std::vector<WordCue>& words,int positionMs){
    if(words.empty())return std::string(line); // ordinary line-timed LRC
    const size_t visible=revealedCount(words,positionMs);
    if(!visible)return {};
    if(visible>=words.size())return std::string(line);

    // Prefer the original text's word boundaries; provider tokens can have
    // punctuation errors, so do not rebuild lyrics from their text field.
    std::vector<size_t> ends;
    for(size_t i=0;i<line.size();){
        while(i<line.size()&&std::isspace(static_cast<unsigned char>(line[i])))++i;
        if(i==line.size())break;
        while(i<line.size()&&!std::isspace(static_cast<unsigned char>(line[i])))++i;
        ends.push_back(i);
    }
    if(ends.size()==words.size())return std::string(line.substr(0,ends[visible-1]));
    if(ends.size()>1){
        const size_t shown=std::clamp(
            (visible*ends.size()+words.size()-1)/words.size(),size_t(1),ends.size());
        return std::string(line.substr(0,ends[shown-1]));
    }

    // Non-spaced scripts must follow the provider's real token boundaries.
    // This also tolerates punctuation present in the display line but omitted
    // by the timed token text.
    const auto aligned=alignedTokenEnds(line,words);
    if(aligned.size()==words.size())
        return std::string(line.substr(0,aligned[visible-1]));

    // For CJK or split syllables, use the relative character lengths of the
    // source tokens, stopping only at a complete UTF-8 code point.
    size_t partial=0,total=0;
    for(size_t i=0;i<words.size();++i){
        const size_t length=utf8Length(words[i].text);
        total+=length;
        if(i<visible)partial+=length;
    }
    if(!total)return {};
    const size_t wanted=std::max<size_t>(1,
        (utf8Length(line)*partial+total-1)/total);
    size_t index=0,characters=0;
    while(index<line.size()&&characters<wanted){
        ++index;
        while(index<line.size()&&
              (static_cast<unsigned char>(line[index])&0xC0)==0x80)++index;
        ++characters;
    }
    return std::string(line.substr(0,index));
}

inline int sourceWordIndex(int displayIndex,int displayCount,int sourceCount){
    if(sourceCount<=0||displayCount<=0)return -1;
    return std::clamp(displayIndex*sourceCount/displayCount,0,sourceCount-1);
}

inline std::string enhancedLrcLine(int lineMs,std::string_view line,
                                   const std::vector<WordCue>& words){
    auto stamp=[](int ms,char open,char close){
        ms=std::max(0,ms);const int hundredths=ms/10;
        std::ostringstream out;
        out<<open<<std::setfill('0')<<std::setw(2)<<hundredths/6000<<':'
           <<std::setw(2)<<(hundredths/100)%60<<'.'<<std::setw(2)<<hundredths%100<<close;
        return out.str();
    };
    std::string output=stamp(lineMs,'[',']');
    if(words.empty()){output+=line;return output;}
    const bool spaced=line.find_first_of(" \t")!=std::string_view::npos;
    for(size_t i=0;i<words.size();++i){
        std::string token=words[i].text;
        const auto first=token.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)continue;
        const auto last=token.find_last_not_of(" \t\r\n");
        token=token.substr(first,last-first+1);
        output+=stamp(words[i].startMs,'<','>');
        output+=token;
        if(spaced&&i+1<words.size()){
            std::string next=words[i+1].text;
            const auto at=next.find_first_not_of(" \t\r\n");
            if(at!=std::string::npos&&std::string(",.!?;:)]}").find(next[at])==std::string::npos&&
               token.back()!='\''&&token.back()!='-')output+=' ';
        }
    }
    if(words.back().endMs>words.back().startMs)
        output+=stamp(words.back().endMs,'<','>');
    return output;
}
}
