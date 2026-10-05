@echo off
setlocal

set "H2MOD_CONFIG=%~1"
if "%H2MOD_CONFIG%"=="" set "H2MOD_CONFIG=Release"
if /I not "%H2MOD_CONFIG%"=="Release" if /I not "%H2MOD_CONFIG%"=="Debug" (
	echo Usage: %~nx0 [Release^|Debug]
	exit /b 2
)

set "ROOT=%~dp0.."
set "SOURCE=%ROOT%\deps\openxr"
set "BUILD=%ROOT%\build\openxr-loader"
set "OUTPUT=%ROOT%\build\bin\x64\%H2MOD_CONFIG%"
set "EXPECTED_OPENXR_COMMIT=57af7fc61f9f2d492580cb28aab6d0ea59d8d417"

if not exist "%SOURCE%\CMakeLists.txt" (
	echo OpenXR-SDK is missing at "%SOURCE%".
	echo Expected commit: %EXPECTED_OPENXR_COMMIT% ^(release-1.1.62^).
	exit /b 3
)

set "ACTUAL_OPENXR_COMMIT="
for /f "delims=" %%I in ('git -C "%SOURCE%" rev-parse HEAD 2^>nul') do set "ACTUAL_OPENXR_COMMIT=%%I"
if not defined ACTUAL_OPENXR_COMMIT (
	echo Failed to read the OpenXR-SDK commit with git -C "%SOURCE%" rev-parse HEAD.
	exit /b 4
)

echo OpenXR-SDK expected commit: %EXPECTED_OPENXR_COMMIT%
echo OpenXR-SDK actual commit:   %ACTUAL_OPENXR_COMMIT%
if not "%ACTUAL_OPENXR_COMMIT%"=="%EXPECTED_OPENXR_COMMIT%" (
	echo OpenXR-SDK commit mismatch. Refusing to build the loader.
	exit /b 5
)

echo Requested h2-mod output configuration: %H2MOD_CONFIG%
echo Official OpenXR loader build configuration: Release
if exist "%BUILD%\CMakeCache.txt" del /q "%BUILD%\CMakeCache.txt"
cmake -S "%SOURCE%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 ^
	-DDYNAMIC_LOADER=ON ^
	-DBUILD_LOADER=ON ^
	-DBUILD_API_LAYERS=OFF ^
	-DBUILD_TESTS=OFF ^
	-DBUILD_CONFORMANCE_TESTS=OFF ^
	-DBUILD_WITH_SYSTEM_JSONCPP=OFF
if errorlevel 1 exit /b 6

cmake --build "%BUILD%" --config Release --target openxr_loader
if errorlevel 1 exit /b 7

if not exist "%OUTPUT%" mkdir "%OUTPUT%"
copy /Y "%BUILD%\src\loader\Release\openxr_loader.dll" "%OUTPUT%\openxr_loader.dll" >nul
if errorlevel 1 exit /b 8

echo Copied Release OpenXR loader to the %H2MOD_CONFIG% h2-mod output: "%OUTPUT%\openxr_loader.dll".
endlocal
