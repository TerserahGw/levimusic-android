cmake_minimum_required(VERSION 3.22)
project(levimusic_android LANGUAGES CXX)
if(NOT ANDROID)
    message(FATAL_ERROR "Build this project with the Android NDK toolchain (arm64-v8a).")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG cc08b40a31471d3e2dedb267f5f17b28da2b0604 # matches LeviLauncher Android v1.7.1 embedded preloader
)
FetchContent_MakeAvailable(preloader_android)

add_library(levimusic SHARED src/LeviMusic.cpp)
add_library(levimusic_standalone SHARED src/LeviMusic.cpp)
target_compile_definitions(levimusic_standalone PRIVATE LEVI_STANDALONE=1)
find_library(ANDROID_LOG_LIB log REQUIRED)
foreach(target IN ITEMS levimusic levimusic_standalone)
    target_include_directories(${target} PRIVATE ${preloader_android_SOURCE_DIR}/include)
    target_compile_options(${target} PRIVATE -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden)
    target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384")
    target_link_libraries(${target} PRIVATE preloader fmt::fmt lunasvg::lunasvg ${ANDROID_LOG_LIB})
endforeach()

set(PACK_DIR "${CMAKE_CURRENT_BINARY_DIR}/levipack")
set(PACK_FILE "${CMAKE_CURRENT_BINARY_DIR}/LeviMusic-1.3.1-arm64-v8a.levipack")
add_custom_target(levipack
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${PACK_DIR}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:levimusic>" "${PACK_DIR}/liblevimusic.so"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md" "${PACK_DIR}/THIRD_PARTY_LICENSES.md"
    COMMAND ${CMAKE_COMMAND} -E chdir "${PACK_DIR}" ${CMAKE_COMMAND} -E tar cf "${PACK_FILE}" --format=zip manifest.json liblevimusic.so THIRD_PARTY_LICENSES.md
    DEPENDS levimusic
    VERBATIM)

# Optional native-only build. In addition to Audius, this variant bundles an
# experimental, VISIBLE Android WebView panel as browser.dex. Browser playback
# remains controlled by the website; no audio extraction or hidden video.
# This standalone variant does not have a full Bridge music API.
# Never enable both native variants simultaneously.
set(STANDALONE_DIR "${CMAKE_CURRENT_BINARY_DIR}/standalone_pack")
set(STANDALONE_FILE "${CMAKE_CURRENT_BINARY_DIR}/LeviMusic-Standalone-1.3.1-arm64-v8a.levipack")
set(BROWSER_DEX "${CMAKE_CURRENT_BINARY_DIR}/browser-dex/classes.dex")
if(NOT EXISTS "${BROWSER_DEX}")
    message(FATAL_ERROR "Standalone browser.dex missing. Compile standalone_browser Java with d8 before CMake build.")
endif()
add_custom_target(levipack_standalone
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${STANDALONE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${STANDALONE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/manifest-standalone.json" "${STANDALONE_DIR}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:levimusic_standalone>" "${STANDALONE_DIR}/liblevimusic_standalone.so"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md" "${STANDALONE_DIR}/THIRD_PARTY_LICENSES.md"
    COMMAND ${CMAKE_COMMAND} -E copy "${BROWSER_DEX}" "${STANDALONE_DIR}/browser.dex"
    COMMAND ${CMAKE_COMMAND} -E chdir "${STANDALONE_DIR}" ${CMAKE_COMMAND} -E tar cf "${STANDALONE_FILE}" --format=zip manifest.json liblevimusic_standalone.so THIRD_PARTY_LICENSES.md browser.dex
    DEPENDS levimusic_standalone
    VERBATIM)


