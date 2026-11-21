@echo off
setlocal
rem Baut mute_soundpack_win64.dll und paketiert sie als .ts3_plugin.
rem Benoetigt MSVC (Visual Studio / Build Tools, x64). Aufruf aus normaler Konsole moeglich.

set VERSION=1.1
set NAME=MuteSoundpack
set DLL=mute_soundpack_win64.dll

where cl >nul 2>nul
if not errorlevel 1 goto :havecl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR echo MSVC nicht gefunden. & exit /b 1
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul || exit /b 1
:havecl

if not exist sdk\include\ts3_functions.h echo SDK fehlt - bitte "git submodule update --init" ausfuehren. & exit /b 1

if exist build rmdir /s /q build
mkdir build\pkg\plugins || exit /b 1

cl /nologo /utf-8 /O1 /GS- /Gs999999 /W4 /Isdk\include /c src\mute_soundpack.c /Fobuild\mute_soundpack.obj || exit /b 1
link /nologo /DLL /NODEFAULTLIB /ENTRY:DllEntry /OUT:build\pkg\plugins\%DLL% build\mute_soundpack.obj kernel32.lib user32.lib || exit /b 1
del build\pkg\plugins\*.lib build\pkg\plugins\*.exp 2>nul

copy /y package\package.ini build\pkg\ >nul || exit /b 1
"%SystemRoot%\System32\tar.exe" -a -c -f build\%NAME%_%VERSION%.zip -C build\pkg package.ini plugins || exit /b 1
move /y build\%NAME%_%VERSION%.zip build\%NAME%_%VERSION%.ts3_plugin >nul || exit /b 1

echo Fertig: build\%NAME%_%VERSION%.ts3_plugin
