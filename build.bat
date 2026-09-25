@echo off
REM Standalone port build helper. Configures + builds the 32-bit game_engine target.
REM Source is this repo root; the game TUs come from the vendored decomp/ tree.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_x86 || exit /b 1
set "CM=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "SRC=%~dp0."
set "BLD=%~dp0build"
"%CM%" -G Ninja -S "%SRC%" -B "%BLD%" -DCMAKE_BUILD_TYPE=Release || exit /b 1
"%CM%" --build "%BLD%" --target game_engine
