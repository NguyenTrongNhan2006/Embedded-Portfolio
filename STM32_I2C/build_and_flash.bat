@echo off
setlocal EnableDelayedExpansion

cd /d "%~dp0"

set "BUILD_DIR=build"
set "BIN_FILE="

if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)

cmake -B "%BUILD_DIR%" -G Ninja
if errorlevel 1 goto error

cmake --build "%BUILD_DIR%"
if errorlevel 1 goto error

for %%F in ("%BUILD_DIR%\*.bin") do (
    if exist "%%~fF" set "BIN_FILE=%%~fF"
)

if not defined BIN_FILE (
    echo ERROR: Khong tim thay file BIN.
    pause
    exit /b 1
)

echo Flashing: !BIN_FILE!

STM32_Programmer_CLI ^
-c port=SWD ^
-w "!BIN_FILE!" 0x08000000 ^
-v ^
-rst

if errorlevel 1 goto error

echo.
echo BUILD AND FLASH SUCCESSFUL
pause
exit /b 0

:error
echo.
echo BUILD OR FLASH FAILED
pause
exit /b 1