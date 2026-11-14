cmake_minimum_required(VERSION 3.20)
project(OnyxiaEXE VERSION 2.0.0 LANGUAGES CXX)

add_executable(OnyxiaEXE WIN32 src/main.cpp)
target_compile_features(OnyxiaEXE PRIVATE cxx_std_20)
target_compile_definitions(OnyxiaEXE PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(OnyxiaEXE PRIVATE comctl32 comdlg32 psapi shell32)

if(MSVC)
    target_compile_options(OnyxiaEXE PRIVATE /W4 /permissive- /utf-8)
    target_link_options(OnyxiaEXE PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:\${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
    )
endif()

set_target_properties(OnyxiaEXE PROPERTIES
    OUTPUT_NAME "OnyxiaEXE"
    WIN32_EXECUTABLE TRUE
)
