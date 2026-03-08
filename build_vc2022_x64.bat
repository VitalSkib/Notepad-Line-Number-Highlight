@echo off

set VC_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022

if exist "%VC_ROOT%\Professional\VC\Auxiliary\Build\vcvarsall.bat" goto UseVcProfessional
if exist "%VC_ROOT%\Community\VC\Auxiliary\Build\vcvarsall.bat" goto UseVcCommunity

goto ErrorNoVcVarsAll

:UseVcProfessional
call "%VC_ROOT%\Professional\VC\Auxiliary\Build\vcvarsall.bat" x64
goto Building

:UseVcCommunity
call "%VC_ROOT%\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
goto Building

:Building
if not exist .\dll_64bit mkdir .\dll_64bit
if exist .\dll_64bit\LineNumberHighlight.dll del /F .\dll_64bit\LineNumberHighlight.dll
cd .\src
cl /O1 /GS- LineNumberHighlight.cpp exports.def /LD /link kernel32.lib user32.lib comctl32.lib comdlg32.lib gdi32.lib Advapi32.lib /OUT:..\dll_64bit\LineNumberHighlight.dll
del *.exp *.lib *.obj
goto End

:ErrorNoVcVarsAll
echo ERROR: Could not find "vcvarsall.bat"
goto End

:End
