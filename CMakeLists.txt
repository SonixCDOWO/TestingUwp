cmake_minimum_required(VERSION 3.21)
project(DxilProbe CXX)

if(NOT WINDOWS_STORE)
    message(FATAL_ERROR
        "Este proyecto es UWP. Configura con:\n"
        "  cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64 \"-DCMAKE_SYSTEM_NAME=WindowsStore\" \"-DCMAKE_SYSTEM_VERSION=10.0\"")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Misma convención que el port de Azahar: bin/<Config>/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(DxilProbe WIN32 src/main.cpp src/probe.cpp src/probe.h)

# C++/WinRT: solo cabeceras del Windows SDK (sin /ZW ni extensiones del compilador).
# Por si la ruta cppwinrt no está en el include por defecto del toolset:
target_include_directories(DxilProbe PRIVATE
    "$(WindowsSdkDir)Include/$(TargetPlatformVersion)/cppwinrt")

# Los proyectos UWP activan /sdl, que convierte _wfopen & co. (C4996) en errores.
target_compile_definitions(DxilProbe PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
target_compile_options(DxilProbe PRIVATE /utf-8 /EHsc /W3 /permissive- /sdl-)
target_link_libraries(DxilProbe PRIVATE d3d12 dxgi d3d11 d2d1 dwrite)
