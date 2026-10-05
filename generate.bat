@echo off
setlocal
pushd "%~dp0" || exit /b 1
git submodule update --init --recursive --jobs 4
if errorlevel 1 (
    popd
    exit /b 1
)
tools\premake5.exe %* vs2022
set "result=%errorlevel%"
popd
exit /b %result%
