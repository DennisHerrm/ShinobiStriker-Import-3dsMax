@echo off
REM ================================================================
REM  BUILD.bat - Shinobi Striker Import bauen und installieren.
REM    BUILD.bat          alle Jahrgaenge 2016-2027 mit installiertem SDK
REM    BUILD.bat 2026     nur diesen
REM  Protokoll: BUILD.log
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "LOG=%~dp0BUILD.log"
set "OUTPUT=%~dp0output"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "JAHRGAENGE=2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027"
set "CMAKE="
set "GEN=Visual Studio 17 2022"
set "GEBAUT=0"
set "NUR=%~1"
set "OHNEPAUSE=%NS_KEIN_PAUSE%"
echo BUILD.bat  %DATE% %TIME%> "%LOG%"
echo.
echo ================================================================
echo   Shinobi Striker Import - bauen und installieren
echo ================================================================

where cmake >nul 2>&1
if not errorlevel 1 (set "CMAKE=cmake" & goto habe_cmake)
if not exist "%VSWHERE%" goto kein_cmake
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath 2^>nul`) do (
  if exist "%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if "%CMAKE%"=="" goto kein_cmake
:habe_cmake
if exist "%VSWHERE%" for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version [18.0^,19.0^) -property installationPath 2^>nul`) do set "GEN=Visual Studio 18 2026"

echo [1/2] Plugin bauen ...
if not "%NUR%"=="" (call :baue %NUR%) else (for %%V in (%JAHRGAENGE%) do call :baue %%V)
if "%GEBAUT%"=="0" goto nichts
echo.
echo [2/2] Installieren ...
set NS_KEIN_PAUSE=1
call "%~dp0INSTALLIERE.bat"
set NS_KEIN_PAUSE=
echo.
echo Fertig: %GEBAUT% Jahrgang/Jahrgaenge gebaut und installiert. Protokoll: %LOG%
goto ende

:baue
set "VER=%~1"
set "SDK=C:\Program Files\Autodesk\3ds Max %VER% SDK\maxsdk"
if not exist "%SDK%\include\max.h" (echo       Max %VER%: kein SDK & goto :eof)
echo       Max %VER% ...
if not exist "build_%VER%\CMakeCache.txt" (
  "%CMAKE%" -S . -B "build_%VER%" -G "%GEN%" -A x64 -DNS_BUILD_PLUGIN=ON -DNS_BUILD_TOOL=OFF -D3DSMAX_SDK_DIR="%SDK%" -DMAX_VERSION=%VER% >> "%LOG%" 2>&1
  if errorlevel 1 goto :fehler
)
REM Alte .dlu weg - sonst gilt ein fehlgeschlagener Bau als ok und die alte wird kopiert.
if exist "build_%VER%\bin\NSImport.dlu" del /q "build_%VER%\bin\NSImport.dlu"
"%CMAKE%" --build "build_%VER%" --config Release >> "%LOG%" 2>&1
if errorlevel 1 goto :fehler
if not exist "build_%VER%\bin\NSImport.dlu" goto :fehler
mkdir "%OUTPUT%\%VER%" >nul 2>&1
copy /Y "build_%VER%\bin\NSImport.dlu" "%OUTPUT%\%VER%\NSImport.dlu" >nul
echo             ok -^> output\%VER%\NSImport.dlu
set /a GEBAUT+=1
goto :eof
:fehler
if exist "%OUTPUT%\%VER%\NSImport.dlu" del /q "%OUTPUT%\%VER%\NSImport.dlu"
echo             FEHLGESCHLAGEN:
findstr /R /C:"error C" /C:"error LNK" /C:"error MSB" "%LOG%" 2>nul
goto :eof

:kein_cmake
echo KEIN CMAKE GEFUNDEN - Visual Studio mit C++ und CMake installieren.
goto ende
:nichts
echo Nichts gebaut - kein 3ds-Max-SDK gefunden oder Baufehler (siehe %LOG%).
:ende
echo.
if not "%OHNEPAUSE%"=="1" pause
