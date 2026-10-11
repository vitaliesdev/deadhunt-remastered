@echo off
rem Builds d3d9.dll (32-bit) with Visual Studio 2022 and checks that every shader compiles.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo Visual Studio with the C++ tools was not found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
set FXC=fxc
where fxc >nul 2>nul || (echo fxc.exe ^(Windows SDK^) was not found & exit /b 1)
for %%E in (PS_FXAA PS_Bright PS_Down PS_BlurH PS_BlurV PS_Final PS_SMAAEdges PS_SMAAWeights PS_SMAABlend PS_AO PS_AOBlurH PS_AOBlurV PS_Depth PS_RayMask PS_RayBlur PS_StoreZ PS_FlashMask PS_FlashReduce PS_One PS_Shadow) do (
  %FXC% /nologo /T ps_3_0 /E %%E /O3 /Fo NUL DeadhuntFX.hlsl || exit /b 1
)
%FXC% /nologo /T vs_3_0 /E VS_Quad /O3 /Fo NUL DeadhuntFX.hlsl || exit /b 1
cl /nologo /LD /O2 /EHsc /W3 /MT DeadhuntFX.cpp /link /DEF:DeadhuntFX.def user32.lib /OUT:d3d9.dll || exit /b 1
echo BUILD OK
