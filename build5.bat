@echo off
echo === Building with msvcrt MinGW ===
set PATH=C:\mingw32rt\bin;%PATH%
cd C:\plugin

del /F /Q LineNumberHighlight.dll 2>nul

x86_64-w64-mingw32-g++ ^
  -std=c++14 -O2 ^
  -DUNICODE -D_UNICODE ^
  -shared ^
  -static-libgcc -static-libstdc++ ^
  -Wl,--kill-at ^
  -o LineNumberHighlight.dll ^
  src/LineNumberHighlight.cpp ^
  src/exports.def ^
  -lkernel32 -luser32 -lcomdlg32 -lgdi32 -ladvapi32

if not exist LineNumberHighlight.dll (
    echo FAILED - compilation error
    pause
    exit /b 1
)

echo.
echo === Dependencies ===
C:\mingw32rt\bin\objdump.exe -p LineNumberHighlight.dll | findstr "DLL Name"

echo.
echo === Copying to Notepad++ ===
del /F /Q "C:\Program Files\Notepad++\plugins\LineNumberHighlight\LineNumberHighlight.dll" 2>nul
copy /Y LineNumberHighlight.dll "C:\Program Files\Notepad++\plugins\LineNumberHighlight\LineNumberHighlight.dll"
if errorlevel 1 (
    echo WARNING: Copy failed - run as Administrator or copy manually
) else (
    echo OK
)

echo.
echo === Verify - file dates must match ===
dir C:\plugin\LineNumberHighlight.dll
dir "C:\Program Files\Notepad++\plugins\LineNumberHighlight\LineNumberHighlight.dll"
pause
