@echo off
rem Deadhunt Remastered - full build from source: generated data, DLL, build\dist and the installer.
rem   build_release.bat "<Deadhunt folder>"
rem The game folder is only read (your untouched Steam copy): fonts and texts are derived from it.
rem The version is the one in the VERSION file.
rem Output: build\release\DeadhuntRemastered_Setup_v<version>.exe
rem Needs: Visual Studio 2022 (C++), Windows 10/11 SDK, Python 3 with Pillow and numpy, Inno Setup 6.
setlocal
cd /d "%~dp0"
set "GAME=%~1"
if "%GAME%"=="" set "GAME=C:\Program Files (x86)\Steam\steamapps\common\Deadhunt"
set /p VER=<VERSION
echo === Deadhunt Remastered %VER% ===
python tools\build\fetch_deps.py || exit /b 1
python tools\build\generate.py "%GAME%" || exit /b 1
call "%~dp0build.bat" || exit /b 1
cd /d "%~dp0"
python tools\build\stage.py || exit /b 1
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
"%ISCC%" /Q installer\DeadhuntRemastered.iss || exit /b 1
echo RELEASE OK: build\release\DeadhuntRemastered_Setup_v%VER%.exe
