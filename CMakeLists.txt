cmake_minimum_required(VERSION 3.20)
project(LightSticker LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "LightSticker targets Windows only.")
endif()

set(LIGHTSTICKER_SOURCES
    src/main.cpp
)

if(WIN32)
    list(APPEND LIGHTSTICKER_SOURCES res/app.rc)
endif()

add_executable(LightSticker WIN32 ${LIGHTSTICKER_SOURCES})

target_compile_features(LightSticker PRIVATE cxx_std_17)
target_compile_definitions(LightSticker PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(LightSticker PRIVATE /W4)
    target_compile_options(LightSticker PRIVATE $<$<CONFIG:Release>:/O2>)
endif()

target_link_libraries(LightSticker PRIVATE user32 gdi32 shell32 ole32)
