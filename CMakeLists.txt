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
target_include_directories(levimusic PRIVATE ${preloader_android_SOURCE_DIR}/include)
target_compile_options(levimusic PRIVATE -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden)
target_link_options(levimusic PRIVATE "-Wl,-z,max-page-size=16384")
find_library(ANDROID_LOG_LIB log REQUIRED)
target_link_libraries(levimusic PRIVATE preloader fmt::fmt lunasvg::lunasvg ${ANDROID_LOG_LIB})

set(PACK_DIR "${CMAKE_CURRENT_BINARY_DIR}/levipack")
set(PACK_FILE "${CMAKE_CURRENT_BINARY_DIR}/LeviMusic-1.0.1-arm64-v8a.levipack")
add_custom_target(levipack
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${PACK_DIR}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:levimusic>" "${PACK_DIR}/liblevimusic.so"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md" "${PACK_DIR}/THIRD_PARTY_LICENSES.md"
    COMMAND ${CMAKE_COMMAND} -E chdir "${PACK_DIR}" ${CMAKE_COMMAND} -E tar cf "${PACK_FILE}" --format=zip manifest.json liblevimusic.so THIRD_PARTY_LICENSES.md
    DEPENDS levimusic
    VERBATIM)
