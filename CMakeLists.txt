cmake_minimum_required(VERSION 3.16)
project(UniversalBorderlessUpscaler LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "This project can only be built on Windows (uses Win32 API + DirectX 11).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    main.cpp
    WindowCapture.cpp
    StyleModifier.cpp
    UpscaleRenderer.cpp
    CursorManager.cpp
    MouseScaler.cpp
)

# Create executable
add_executable(UBU ${SOURCES})

# Compiler flags
if(MSVC)
    target_compile_options(UBU PRIVATE /O2)
else()
    target_compile_options(UBU PRIVATE -O2)
    target_link_options(UBU PRIVATE "SHELL:-mwindows" "SHELL:-municode")
endif()

# Link libraries
target_link_libraries(UBU PRIVATE
    d3d11
    d3dcompiler
    dxgi
    gdi32
    user32
)
