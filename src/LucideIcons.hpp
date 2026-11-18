#pragma once
// Official Lucide SVG glyphs (https://github.com/lucide-icons/lucide)
// Copyright (c) Lucide Icons contributors, ISC license. See THIRD_PARTY_LICENSES.md.
// Rasterized with lunaSVG included with the Preloader SDK.
#include <pl/ModMenu.hpp>
#include <lunasvg.h>
#include <array>
#include <memory>
#include <span>
#include <string>
namespace levimusic_icons {
struct SvgIcon { const char* name; const char* svg; };
inline constexpr std::array<SvgIcon,15> icons = {{
    {"play",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z" /> </svg>)lvsvg"},
    {"pause",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <rect x="14" y="3" width="5" height="18" rx="1" /> <rect x="5" y="3" width="5" height="18" rx="1" /> </svg>)lvsvg"},
    {"skip-back",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M17.971 4.285A2 2 0 0 1 21 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432z" /> <path d="M3 20V4" /> </svg>)lvsvg"},
    {"skip-forward",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M21 4v16" /> <path d="M6.029 4.285A2 2 0 0 0 3 6v12a2 2 0 0 0 3.029 1.715l9.997-5.998a2 2 0 0 0 .003-3.432z" /> </svg>)lvsvg"},
    {"square",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <rect width="18" height="18" x="3" y="3" rx="2" /> </svg>)lvsvg"},
    {"search",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="m21 21-4.34-4.34" /> <circle cx="11" cy="11" r="8" /> </svg>)lvsvg"},
    {"plus",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 5v14"/><path d="M5 12h14"/></svg>)lvsvg"},
    {"x",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M18 6 6 18" /> <path d="m6 6 12 12" /> </svg>)lvsvg"},
    {"chevron-left",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="m15 18-6-6 6-6" /> </svg>)lvsvg"},
    {"chevron-right",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="m9 18 6-6-6-6" /> </svg>)lvsvg"},
    {"disc-3",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <circle cx="12" cy="12" r="10" /> <path d="M6 12c0-1.7.7-3.2 1.8-4.2" /> <circle cx="12" cy="12" r="2" /> <path d="M18 12c0 1.7-.7 3.2-1.8 4.2" /> </svg>)lvsvg"},
    {"settings-2",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M14 17H5" /> <path d="M19 7h-9" /> <circle cx="17" cy="17" r="3" /> <circle cx="7" cy="7" r="3" /> </svg>)lvsvg"},
    {"volume-2",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z" /> <path d="M16 9a5 5 0 0 1 0 6" /> <path d="M19.364 18.364a9 9 0 0 0 0-12.728" /> </svg>)lvsvg"},
    {"list-music",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M16 5H3" /> <path d="M11 12H3" /> <path d="M11 19H3" /> <path d="M21 16V5" /> <circle cx="18" cy="16" r="3" /> </svg>)lvsvg"},
    {"audio-lines",R"lvsvg(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#F4FFFF" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" > <path d="M2 10v3" /> <path d="M6 6v11" /> <path d="M10 3v18" /> <path d="M14 8v7" /> <path d="M18 5v13" /> <path d="M22 10v3" /> </svg>)lvsvg"},
}};
inline void registerAll() {
    for (const auto& icon : icons) {
        std::unique_ptr<lunasvg::Document> doc = lunasvg::Document::loadFromData(icon.svg);
        if (!doc) continue;
        lunasvg::Bitmap bitmap = doc->renderToBitmap(64,64,0x00000000);
        if (bitmap.isNull()) continue;
        bitmap.convertToRGBA();
        const auto* pixels=bitmap.data();
        const size_t length=static_cast<size_t>(bitmap.width())*bitmap.height()*4;
        pl::modmenu::registerImage(std::string("levimusic.icon.")+icon.name,
                                   std::span<const unsigned char>(pixels,length),
                                   bitmap.width(),bitmap.height());
    }
}
}
