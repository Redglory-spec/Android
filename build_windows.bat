@echo off
setlocal
echo ===================================================================
echo   MEMBANGUN BOT KEEP STOCK OTOMATIS v5.9 (NATIVE WIN32 C++17)
echo ===================================================================

where g++ >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Compiler MinGW-w64 ^(g++^) terdeteksi. Mengompilasi...
    where windres >nul 2>nul
    if %ERRORLEVEL% EQU 0 (
        windres resource.rc -O coff -o resource.res
        g++ -std=c++17 -O2 -Wall -municode -mwindows main.cpp resource.res -o BotKeepStock.exe ^
            -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lwinhttp -lshlwapi -lshell32 -ladvapi32 ^
            -static -static-libgcc -static-libstdc++ -s
        del /q resource.res 2>nul
    ) else (
        g++ -std=c++17 -O2 -Wall -municode -mwindows main.cpp -o BotKeepStock.exe ^
            -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lwinhttp -lshlwapi -lshell32 -ladvapi32 ^
            -static -static-libgcc -static-libstdc++ -s
    )
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo [SUKSES] BotKeepStock.exe berhasil dibuat! ^(Mandiri / Tanpa DLL tambahan^)
        goto :end
    ) else (
        echo [ERROR] Kompilasi gagal.
        goto :end
    )
)

where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
        for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
                call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
            )
        )
    )
)

where cl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Compiler Microsoft Visual C++ ^(cl.exe^) terdeteksi. Mengompilasi...
    rc /nologo resource.rc
    cl /nologo /EHsc /O2 /std:c++17 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX main.cpp resource.res ^
       /Fe:BotKeepStock.exe /link /SUBSYSTEM:WINDOWS ^
       user32.lib gdi32.lib comctl32.lib comdlg32.lib winhttp.lib shlwapi.lib shell32.lib advapi32.lib
    del /q main.obj resource.res 2>nul
    if %ERRORLEVEL% EQU 0 (
        echo.
        echo [SUKSES] BotKeepStock.exe berhasil dibuat!
        goto :end
    )
)

echo [PERINGATAN] Compiler C++ ^(g++ atau cl.exe^) belum ditemukan di PATH.
echo Silakan install MinGW-w64 ^(atau MSYS2 / Visual Studio Build Tools^) terlebih dahulu.

:end
pause
