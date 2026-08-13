# Cross-compile Atlas Launcher for 64-bit Windows using MinGW-w64 and Qt 5.15.2.
# Override QT_ROOT when a different Windows Qt package is used.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

if(NOT DEFINED QT_ROOT)
    set(QT_ROOT "/home/ubuntu/qt-win64/5.15.2/mingw81_64")
endif()

set(CMAKE_PREFIX_PATH "${QT_ROOT}" CACHE PATH "Windows Qt prefix")
set(CMAKE_FIND_ROOT_PATH "${QT_ROOT};/usr/x86_64-w64-mingw32" CACHE STRING "Cross compile root paths")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# A cross-compiled executable cannot run as a CMake feature test in this host.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
