@echo off
chcp 65001 >nul
rem =====================================================================
rem  Arma la carpeta "binarios" con la ultima compilacion.
rem  1) En Arduino IDE: Sketch > Export Compiled Binary
rem  2) Doble clic en este archivo
rem =====================================================================
setlocal
set "AQUI=%~dp0"
set "BUILD=%AQUI%build\esp32.esp32.esp32cam"
set "CORE=%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\1.0.6\tools"
set "ESPTOOL=%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
set "SALIDA=%AQUI%binarios"

if not exist "%BUILD%\ESP32_CASILLERO_FACIAL.ino.bin" (
  echo [ERROR] No encuentro la compilacion en:
  echo   %BUILD%
  echo Primero usa en Arduino IDE: Sketch ^> Export Compiled Binary
  pause & exit /b 1
)
if not exist "%ESPTOOL%" (
  echo [ERROR] No encuentro esptool en:
  echo   %ESPTOOL%
  pause & exit /b 1
)
if not exist "%SALIDA%" mkdir "%SALIDA%"

copy /y "%CORE%\sdk\bin\bootloader_qio_80m.bin"                  "%SALIDA%\1_bootloader_0x1000.bin"  >nul || goto fallo
copy /y "%BUILD%\ESP32_CASILLERO_FACIAL.ino.partitions.bin"      "%SALIDA%\2_particiones_0x8000.bin" >nul || goto fallo
copy /y "%CORE%\partitions\boot_app0.bin"                        "%SALIDA%\3_boot_app0_0xe000.bin"   >nul || goto fallo
copy /y "%BUILD%\ESP32_CASILLERO_FACIAL.ino.bin"                 "%SALIDA%\4_programa_0x10000.bin"   >nul || goto fallo

"%ESPTOOL%" --chip esp32 merge_bin -o "%SALIDA%\CASILLERO_FACIAL_completo_0x0.bin" ^
  --flash_mode dio --flash_freq 80m --flash_size 4MB ^
  0x1000  "%SALIDA%\1_bootloader_0x1000.bin" ^
  0x8000  "%SALIDA%\2_particiones_0x8000.bin" ^
  0xe000  "%SALIDA%\3_boot_app0_0xe000.bin" ^
  0x10000 "%SALIDA%\4_programa_0x10000.bin" || goto fallo

echo.
echo Listo. Archivos en: %SALIDA%
dir /b "%SALIDA%\*.bin"
echo.
echo Graba SOLO "CASILLERO_FACIAL_completo_0x0.bin" en 0x0 (DIO, 80 MHz, 4 MB)
pause
exit /b 0

:fallo
echo [ERROR] Algo fallo al armar los binarios.
pause
exit /b 1
