@echo off
:: build.bat — builds the 32-bit proxy dinput8.dll with MinHook
:: Run from "x86 Native Tools Command Prompt for VS 20xx"
setlocal

set OUT=out/dinput8.dll
set SRC=dinput8_proxy.cpp
set DEF=dinput8.def

set MINHOOK_SRC=deps\minhook\src\buffer.c deps\minhook\src\hook.c deps\minhook\src\trampoline.c deps\minhook\src\hde\hde32.c

echo [*] Compiling...
cl.exe ^
    /nologo ^
    /O2 ^
    /W3 ^
    /EHsc ^
    /MT ^
    /D "WIN32" ^
    /D "NDEBUG" ^
    /D "_WINDOWS" ^
    /D "_USRDLL" ^
    /I "deps\minhook\src" ^
    /LD ^
    %SRC% %MINHOOK_SRC% ^
    /Fe%OUT% ^
    /link ^
    /DEF:%DEF% ^
    /SUBSYSTEM:WINDOWS ^
    /MACHINE:X86 ^
    dinput8.lib ^
    dxguid.lib ^
    d3d11.lib ^
    dxgi.lib ^
    kernel32.lib ^
    user32.lib

if %ERRORLEVEL% NEQ 0 (
    echo [!] Build FAILED.
    exit /b 1
)

echo.
echo [OK] Built %OUT%
echo      Copy dinput8.dll + dinput8_proxy.ini to the game folder.
endlocal
