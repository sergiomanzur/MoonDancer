@echo off
setlocal
if not exist build\mwalk_launcher.exe (
  echo Build the project first: build_windows.bat
  exit /b 1
)
if not exist build\mwalk.exe (
  echo Build the project first: build_windows.bat
  exit /b 1
)
if exist dist rmdir /s /q dist
mkdir dist\moonwalker-windows
copy build\mwalk_launcher.exe dist\moonwalker-windows\ >nul
copy build\mwalk.exe dist\moonwalker-windows\ >nul
copy build\SDL2.dll dist\moonwalker-windows\ >nul
copy README.md dist\moonwalker-windows\ >nul
copy LICENSE dist\moonwalker-windows\ >nul
copy THIRD_PARTY_NOTICES.md dist\moonwalker-windows\ >nul
xcopy /E /I /Y assets\* dist\moonwalker-windows\assets\ >nul
mkdir dist\moonwalker-windows\docs
copy docs\launcher.png dist\moonwalker-windows\docs\ >nul
copy docs\settings.png dist\moonwalker-windows\docs\ >nul
copy docs\screenshot.png dist\moonwalker-windows\docs\ >nul
copy docs\gameplay.png dist\moonwalker-windows\docs\ >nul
copy docs\PORTING_NOTES.md dist\moonwalker-windows\docs\ >nul
echo.
echo Created dist\moonwalker-windows.
echo Put your mwalk.zip next to mwalk_launcher.exe (or use BROWSE), then run:
echo   dist\moonwalker-windows\mwalk_launcher.exe
