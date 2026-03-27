@echo off
REM Get the directory of the batch script
set "SCRIPT_DIR="%~dp0\""

REM Navigate to the directory containing the Python script
cd /d "%SCRIPT_DIR%.pio\libdeps\default\STM32 Framework"

REM Call the Python script with the base path argument, using relative paths
python Update.py %SCRIPT_DIR%