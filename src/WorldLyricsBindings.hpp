#pragma once
// Binding evidence: callmehuda/World-Analysis @ 4669e3072f79ede9b074d7d1ac19df4b9de88cee.
// Copyright 2026 Ekowat; derived from BedrockTools by RadiantByte. GPL-3.0-only.
// Keep each ELF identity, RVA, instruction pattern and object layout together.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace lm_world3d {
struct Binding { std::uintptr_t rva; std::string_view pattern; };
enum BindingId { Tick, Render, Begin, Color, Vertex, Mesh, MaterialGroup, BindingCount };
struct Profile {
    std::string_view name, buildId;
    std::size_t rendererPlayer, camera, tessellator, rotation, level;
    std::array<Binding, BindingCount> bindings;
};
inline constexpr std::array<Profile,4> profiles{{
    {"1.26.52", "56de9eed077631e03a31f4f58eb2f0e00071d335", 0x420, 0x61c, 0xb8, 0x218, 0x1d0, {{
        {0xaac7dd8, "E8 0F 19 FC FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 F4 4F 06 A9 FD 43 00 91 FF C3 09 D1 54 D0 3B D5 F3 03 00 AA 88 16 40 F9 A8 03 1E F8 08 C0 78 39 ? ? ? ? 68 92 4C B9"}, // NormalTick
        {0xb318478, "EE 0F 16 FC ED 33 01 6D EB 2B 02 6D E9 23 03 6D FD 7B 04 A9 FC 6F 05 A9 FA 67 06 A9 F8 5F 07 A9 F6 57 08 A9 F4 4F 09 A9 FD 03 01 91 FF 03 06 D1 57 D0 3B D5 F8 03 00 AA F4 03 02 AA E8 16 40 F9"}, // RenderLevel
        {0xa76e7f8, "FD 7B BC A9 F8 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 08 60 4A 39 09 54 49 39 08 01 09 2A 28 0E 00 35 F3 03 00 AA 1F 90 02 B9 F6 03 04 2A 1F 54 09 39 F4 03 03 2A F5 03 02 2A 1F 60 0A 39"}, // TessellatorBegin
        {0xa76edac, "E8 6F A8 52 0C 50 49 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E 08 00 38 1E 29 00 38 1E 4A 00 38 1E 6B 00 38 1E EC 02 00 37 6B 7D AB 0A EC 1F 80 52 4A 7D AA 0A 29 7D A9 0A"}, // TessellatorColor
        {0xa76f048, "FF C3 02 D1 EA 1B 00 FD E9 23 04 6D FD 7B 05 A9 FC 6F 06 A9 FA 67 07 A9 F8 5F 08 A9 F6 57 09 A9 F4 4F 0A A9 FD 43 01 91 5A D0 3B D5 48 17 40 F9 E8 17 00 F9 08 90 42 B9 09 94 42 B9 1F 01 09 6B"}, // TessellatorVertex
        {0xb28402c, "FD 7B BC A9 FC 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 FF 03 0A D1 57 D0 3B D5 F6 03 00 AA E0 03 01 AA E8 16 40 F9 F4 03 03 AA F5 03 02 AA F3 03 01 AA A8 83 1F F8 13 AB D3 97 C0 02 00 37"}, // MeshHelpersRenderMeshImmediately2
        {0x997a740, "E0 CF 04 90 00 A0 13 91 A1 83 00 D1 A2 43 01 D1 E4 83 01 91 E3 03 14 AA 25 00 80 52 13 A4 3B 94 F4 37 40 F9 FF 7F 06 A9 F4 06 00 B4 81 22 00 91 00 00 80 92 87 BA AE 97 E0 00 00 B5 88 02 40 F9"}, // RenderMaterialGroupCommon
    }}},
    {"1.26.62.1", "c60fa32908019eecae39ebfe7bac5755dc35cde2", 0x430, 0x614, 0xb0, 0x238, 0x1f0, {{
        {0xaf5e7a0, "E8 0F 19 FC FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 F4 4F 06 A9 FD 43 00 91 FF C3 09 D1 54 D0 3B D5 F3 03 00 AA 88 16 40 F9"},
        {0xb7a38a0, "FF 83 07 D1 EE AB 00 FD ED 33 16 6D EB 2B 17 6D E9 23 18 6D FD 7B 19 A9 FC 67 1A A9 F8 5F 1B A9"},
        {0xac2a748, "FD 7B BC A9 F8 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 08 60 4A 39 09 54 49 39 08 01 09 2A"},
        {0xac2acfc, "E8 6F A8 52 0C 50 49 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E"},
        {0xac2af98, "FF C3 02 D1 EA 1B 00 FD E9 23 04 6D FD 7B 05 A9 FC 6F 06 A9 FA 67 07 A9 F8 5F 08 A9 F6 57 09 A9 F4 4F 0A A9"},
        {0xb769fec, "FD 7B BC A9 FC 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 FF C3 09 D1 57 D0 3B D5 F6 03 00 AA"},
        {0x9d21a3c, "A0 1B 05 F0 00 80 38 91 A1 83 00 D1 A2 43 01 D1 E4 83 01 91 E3 03 14 AA 25 00 80 52"},
    }}},
    {"1.26.66.1", "e96ddd68541814923c35460f8b99f27a66c51232", 0x430, 0x614, 0xb0, 0x238, 0x1f0, {{
        {0xaf5eb10, "E8 0F 19 FC FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 F4 4F 06 A9 FD 43 00 91 FF C3 09 D1 54 D0 3B D5 F3 03 00 AA 88 16 40 F9"},
        {0xb7a3c80, "FF 83 07 D1 EE AB 00 FD ED 33 16 6D EB 2B 17 6D E9 23 18 6D FD 7B 19 A9 FC 67 1A A9 F8 5F 1B A9 F6 57 1C A9 F4 4F 1D A9 FD 43 06 91 57 D0 3B D5"},
        {0xac2aab8, "FD 7B BC A9 F8 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 08 60 4A 39 09 54 49 39 08 01 09 2A"},
        {0xac2b06c, "E8 6F A8 52 0C 50 49 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E 08 00 38 1E"},
        {0xac2b308, "FF C3 02 D1 EA 1B 00 FD E9 23 04 6D FD 7B 05 A9 FC 6F 06 A9 FA 67 07 A9 F8 5F 08 A9 F6 57 09 A9 F4 4F 0A A9 FD 43 01 91 5B D0 3B D5 68 17 40 F9"},
        {0xb76a3cc, "FD 7B BC A9 FC 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 FF C3 09 D1 57 D0 3B D5 F6 03 00 AA"},
        {0x9d21b1c, "C0 1B 05 90 00 80 0D 91 A1 83 00 D1 A2 43 01 D1 E4 83 01 91 E3 03 14 AA 25 00 80 52 31 DA 3B 94 F4 37 40 F9"},
    }}},
    {"1.26.60", "eb291b883dfdfaff70c4999c2eabd61885ae3e15", 0x430, 0x614, 0xb0, 0x238, 0x1f0, {{
        {0xaf5e460, "E8 0F 19 FC FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 F4 4F 06 A9 FD 43 00 91 FF C3 09 D1 54 D0 3B D5 F3 03 00 AA 88 16 40 F9"}, // NormalTick
        {0xb7a35d8, "FF 83 07 D1 EE AB 00 FD ED 33 16 6D EB 2B 17 6D E9 23 18 6D FD 7B 19 A9 FC 67 1A A9 F8 5F 1B A9"}, // RenderLevel
        {0xac2a408, "FD 7B BC A9 F8 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 08 60 4A 39 09 54 49 39 08 01 09 2A"}, // TessellatorBegin
        {0xac2a9bc, "E8 6F A8 52 0C 50 49 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E 08 00 38 1E"}, // TessellatorColor
        {0xac2ac58, "FF C3 02 D1 EA 1B 00 FD E9 23 04 6D FD 7B 05 A9 FC 6F 06 A9 FA 67 07 A9 F8 5F 08 A9 F6 57 09 A9 F4 4F 0A A9 FD 43 01 91 5B D0 3B D5 68 17 40 F9"}, // TessellatorVertex
        {0xb769d24, "FD 7B BC A9 FC 5F 01 A9 F6 57 02 A9 F4 4F 03 A9 FD 03 00 91 FF C3 09 D1 57 D0 3B D5 F6 03 00 AA"}, // MeshHelpersRenderMeshImmediately2
        {0x9d21798, "A0 1B 05 F0 00 80 1F 91 A1 83 00 D1 A2 43 01 D1 E4 83 01 91 E3 03 14 AA 25 00 80 52 66 D9 3B 94"}, // RenderMaterialGroupCommon
    }}}
}};
inline bool matchesBuildId(std::span<const std::uint8_t> notes, std::string_view id) {
    const auto word=[&](std::size_t at) {
        return std::uint32_t(notes[at]) | (std::uint32_t(notes[at+1])<<8)
            | (std::uint32_t(notes[at+2])<<16) | (std::uint32_t(notes[at+3])<<24);
    };
    constexpr char hex[]="0123456789abcdef";
    while(notes.size()>=12) {
        const std::uint64_t n=word(0), v=word(4);
        const auto type=word(8);
        const auto pn=(n+3)&~std::uint64_t(3), pv=(v+3)&~std::uint64_t(3);
        if(pn>notes.size()-12 || pv>notes.size()-12-pn) return false;
        if(type==3 && n==4 && notes[12]=='G' && notes[13]=='N' && notes[14]=='U' && !notes[15]) {
            if(v*2!=id.size()) return false;
            for(std::size_t i=0;i<v;++i) {
                auto b=notes[12+pn+i];
                if(id[2*i]!=hex[b>>4] || id[2*i+1]!=hex[b&15]) return false;
            }
            return true;
        }
        notes=notes.subspan(static_cast<std::size_t>(12+pn+pv));
    }
    return false;
}
inline bool matchesPattern(std::span<const std::uint8_t> bytes,std::string_view pattern) {
    std::size_t i=0;
    auto hex=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    while(!pattern.empty()) {
        const auto end=pattern.find(' ');
        const auto token=pattern.substr(0,end);
        if(i>=bytes.size())return false;
        if(token!="?" && token!="??") {
            if(token.size()!=2 || hex(token[0])<0 || hex(token[1])<0
                || bytes[i]!=(hex(token[0])*16+hex(token[1]))) return false;
        }
        ++i;
        if(end==std::string_view::npos) return true;
        pattern.remove_prefix(end+1);
    }
    return false;
}
inline std::size_t patternBytes(std::string_view pattern) {
    if(pattern.empty())return 0;
    std::size_t count=1;
    for(char c:pattern)if(c==' ')++count;
    return count;
}
} // namespace lm_world3d


