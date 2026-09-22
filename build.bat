@echo off
title Universal Borderless Upscaler - Builder

echo ===================================================
echo   Building Universal Borderless Upscaler...
echo ===================================================
echo.

g++ -std=c++17 -O2 -mwindows -municode ^
    main.cpp ^
    WindowCapture.cpp ^
    StyleModifier.cpp ^
    UpscaleRenderer.cpp ^
    CursorManager.cpp ^
    MouseScaler.cpp ^
    -o UniversalBorderlessUpscaler.exe ^
    -ld3d11 -ld3dcompiler -ldxgi -lgdi32 -luser32 -lpthread

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Build failed! Please check the errors above.
    echo.
    pause
    exit /b %errorlevel%
)

echo.
echo [SUCCESS] UniversalBorderlessUpscaler.exe built successfully!
echo.
set /p run="Run the program now? (Y/N): "
if /i "%run%"=="Y" (
    start "" UniversalBorderlessUpscaler.exe
)