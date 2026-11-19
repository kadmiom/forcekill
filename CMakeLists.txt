cmake_minimum_required(VERSION 3.20)
project(ForceKill VERSION 1.0.1 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR
        "ForceKill is a Windows application. Configure with a MinGW-w64 toolchain, "
        "for example: cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ForceKill WIN32
    src/main.cpp
    src/autostart.cpp
    src/hotkey.cpp
    src/killer.cpp
    src/log.cpp
    src/settings.cpp
    src/settings_ui.cpp
    src/tray.cpp
    src/resource.rc
)

target_include_directories(ForceKill PRIVATE src)

target_compile_definitions(ForceKill PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(ForceKill PRIVATE
    user32
    shell32
    gdi32
    advapi32
)

if(MSVC)
    target_compile_options(ForceKill PRIVATE /W4 /utf-8 /permissive-)
    # resource.rc embeds our own RT_MANIFEST, so stop the linker from adding a
    # second one (CVT1100: duplicate resource, type:MANIFEST, name:1).
    target_link_options(ForceKill PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
else()
    target_compile_options(ForceKill PRIVATE -Wall -Wextra)
    # Ship without MinGW runtime DLLs next to the exe.
    target_link_options(ForceKill PRIVATE -static -static-libgcc -static-libstdc++)
endif()

set_target_properties(ForceKill PROPERTIES OUTPUT_NAME "ForceKill")
