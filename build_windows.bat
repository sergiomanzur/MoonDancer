@echo off
setlocal
if not exist build (
  cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build || exit /b 1
echo.
echo Built build\mwalk_launcher.exe and build\mwalk.exe
echo Run build\mwalk_launcher.exe, select your mwalk.zip, and press START GAME.
