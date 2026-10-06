@echo off
setlocal
set "LOADER_OUTPUT_CONFIG=%~1"
if "%LOADER_OUTPUT_CONFIG%"=="" set "LOADER_OUTPUT_CONFIG=RelWithDebInfo"
if /I not "%LOADER_OUTPUT_CONFIG%"=="Release" if /I not "%LOADER_OUTPUT_CONFIG%"=="Debug" if /I not "%LOADER_OUTPUT_CONFIG%"=="RelWithDebInfo" (
    echo Usage: %~nx0 [RelWithDebInfo^|Release^|Debug]
    exit /b 2
)
rem Both clients share the same optimized loader in a game installation.
python "%~dp0build_openxr_loader.py" --configuration RelWithDebInfo --output-dir "%~dp0..\build\bin\x64\%LOADER_OUTPUT_CONFIG%"
exit /b %errorlevel%
