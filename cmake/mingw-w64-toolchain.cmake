# Cross-compile ForceKill for Windows x64 from Linux/macOS.
#
# Works with a MinGW-w64 GCC toolchain or an LLVM-MinGW toolchain:
#
#   MINGW_PREFIX=/path/to/llvm-mingw cmake -S . -B build-mingw \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-mingw
#
# MINGW_PREFIX may be omitted when the tools are already on PATH.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX "$ENV{MINGW_PREFIX}" CACHE PATH "MinGW-w64 / LLVM-MinGW prefix")

if(MINGW_PREFIX)
    set(_mingw_hints HINTS "${MINGW_PREFIX}/bin")
else()
    set(_mingw_hints)
endif()

find_program(CMAKE_C_COMPILER
    NAMES x86_64-w64-mingw32-gcc x86_64-w64-mingw32-clang
    ${_mingw_hints} REQUIRED)
find_program(CMAKE_CXX_COMPILER
    NAMES x86_64-w64-mingw32-g++ x86_64-w64-mingw32-clang++
    ${_mingw_hints} REQUIRED)
find_program(CMAKE_RC_COMPILER
    NAMES x86_64-w64-mingw32-windres llvm-windres
    ${_mingw_hints} REQUIRED)

if(MINGW_PREFIX)
    set(CMAKE_FIND_ROOT_PATH "${MINGW_PREFIX}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endif()
