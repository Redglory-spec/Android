cmake_minimum_required(VERSION 3.15)
project(BotKeepStock VERSION 5.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_executable(BotKeepStock WIN32
        main.cpp
        resource.rc
    )

    target_compile_definitions(BotKeepStock PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)

    target_link_libraries(BotKeepStock PRIVATE
        user32
        gdi32
        comctl32
        comdlg32
        winhttp
        shlwapi
        shell32
        advapi32
    )

    if(MINGW)
        target_link_options(BotKeepStock PRIVATE -static -static-libgcc -static-libstdc++ -municode)
    endif()
else()
    message(WARNING "Target utama proyek ini adalah Windows (Win32 API). Gunakan MinGW-w64 untuk cross-compile dari Linux.")
endif()
