@echo off
REM ===========================================================================
REM  BAUE_RELEASE.bat - Setup.exe und ZIP fuer eine Veroeffentlichung bauen
REM
REM  Aufruf:  installer\BAUE_RELEASE.bat [ordner mit <jahr>\NSImport.dlu]
REM           ohne Angabe: output\ (von BUILD.bat)
REM
REM  Ergebnis in dist\:
REM    NSImport-<version>-Setup.exe   Installer (Inno Setup 6)
REM    NSImport-<version>.zip         Paket + Install.bat/Uninstall.bat/README.txt
REM    SHA256SUMS.txt
REM
REM  Signieren (optional): ist NS_SIGNTOOL gesetzt, wird damit jede .dlu und
REM  die Setup.exe signiert. Ohne Signatur zeigt SmartScreen eine Warnung.
REM  (Aufbau uebernommen vom SWBF2 Import.)
REM ===========================================================================
setlocal enabledelayedexpansion
REM  Aus PowerShell 7 gestartet, erbt Windows PowerShell 5.1 deren Modulpfad.
set "PSModulePath="
cd /d "%~dp0.."
set "WURZEL=%CD%"
set "QUELLE=%~1"
if "%QUELLE%"=="" set "QUELLE=%WURZEL%\output"
set "DIST=%WURZEL%\dist"
set "PAKET=%DIST%\paket\NSImport"
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
set "REDIST=%WURZEL%\installer\redist\vc_redist.x64.exe"

for /f "usebackq delims=" %%v in (`powershell -NoProfile -Command "([xml](Get-Content -Raw '%WURZEL%\package\NSImport\PackageContents.xml')).ApplicationPackage.AppVersion"`) do set "VER=%%v"
if not defined VER (echo FEHLER: Version nicht lesbar & exit /b 1)
echo.
echo  Shinobi Striker Import %VER% - Release bauen
echo  Quelle der Plugins: %QUELLE%
echo.

if not exist "%ISCC%" (echo FEHLER: Inno Setup 6 fehlt: !ISCC! & exit /b 1)

REM ---- Paket zusammenstellen ------------------------------------------------
if exist "%DIST%\paket" rmdir /s /q "%DIST%\paket"
mkdir "%PAKET%\Contents\MacroScripts" "%PAKET%\Contents\Pre-Start-Up_Scripts" "%PAKET%\Contents\Post-Start-Up_Scripts"
copy /Y "%WURZEL%\package\NSImport\PackageContents.xml" "%PAKET%\" >nul
copy /Y "%WURZEL%\scripts\NSImport.mcr" "%PAKET%\Contents\MacroScripts\" >nul
copy /Y "%WURZEL%\scripts\NSMenu_2025_2027.ms" "%PAKET%\Contents\Pre-Start-Up_Scripts\" >nul
copy /Y "%WURZEL%\scripts\NSMenu_2016_2024.ms" "%PAKET%\Contents\Post-Start-Up_Scripts\" >nul
set JAHRE=0
for %%J in (2016 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026 2027) do (
  if exist "%QUELLE%\%%J\NSImport.dlu" (
    mkdir "%PAKET%\Contents\%%J"
    copy /Y "%QUELLE%\%%J\NSImport.dlu" "%PAKET%\Contents\%%J\" >nul
    set /a JAHRE+=1
  ) else (
    echo  WARNUNG: keine NSImport.dlu fuer 3ds Max %%J
  )
)
if "%JAHRE%"=="0" (echo FEHLER: keine einzige .dlu in !QUELLE! - erst BUILD.bat & exit /b 1)
echo  %JAHRE% Max-Jahrgaenge im Paket.

REM ---- Jede .dlu: 64-Bit-DLL und richtige Version? ---------------------------
for /R "%PAKET%\Contents" %%f in (NSImport.dl*) do if /i "%%~xf"==".dlu" (
  set "ERG="
  for /f "usebackq delims=" %%r in (`powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%%f'); $e=[BitConverter]::ToInt32($b,60); if($b[0] -ne 0x4D -or $b[$e] -ne 0x50 -or [BitConverter]::ToUInt16($b,$e+4) -ne 0x8664){'kaputt'} else {(Get-Item '%%f').VersionInfo.FileVersion}"`) do set "ERG=%%r"
  echo    %%~dpf : !ERG!
  if /i "!ERG!"=="kaputt" (echo FEHLER: keine gueltige 64-Bit-DLL: %%f & exit /b 1)
  if /i not "!ERG!"=="%VER%" (echo FEHLER: %%f hat Version !ERG!, erwartet %VER% - erst BUILD.bat & exit /b 1)
)

if defined NS_SIGNTOOL (
  echo  Signiere die .dlu-Dateien ...
  for /R "%PAKET%\Contents" %%f in (NSImport.dl*) do if /i "%%~xf"==".dlu" %NS_SIGNTOOL% "%%f" || (echo FEHLER beim Signieren & exit /b 1)
)

REM ---- VC++-Laufzeit von Microsoft (fuer die Setup.exe) ----------------------
if not exist "%REDIST%" (
  echo  Lade vc_redist.x64.exe von Microsoft ...
  mkdir "%WURZEL%\installer\redist" >nul 2>&1
  pwsh -NoProfile -Command "Invoke-WebRequest -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile '%REDIST%'" >nul 2>&1
  if not exist "%REDIST%" powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile '%REDIST%'" >nul 2>&1
  if not exist "%REDIST%" curl.exe -L -s -S -o "%REDIST%" "https://aka.ms/vc14/vc_redist.x64.exe"
)
for /f "usebackq delims=" %%s in (`powershell -NoProfile -Command "$s=Get-AuthenticodeSignature '%REDIST%'; if($s.Status -eq 'Valid' -and $s.SignerCertificate.Subject -like '*Microsoft Corporation*'){'ok'}else{'NICHT GUELTIG'}"`) do set "SIG=%%s"
if /i not "%SIG%"=="ok" (echo FEHLER: vc_redist.x64.exe ist nicht von Microsoft signiert & exit /b 1)
echo  vc_redist.x64.exe: Microsoft-Signatur gueltig.

REM ---- Setup.exe ------------------------------------------------------------
"%ISCC%" /Q /DAppVer=%VER% "%WURZEL%\installer\NSImport.iss"
if errorlevel 1 (echo FEHLER: Inno Setup & exit /b 1)
if defined NS_SIGNTOOL %NS_SIGNTOOL% "%DIST%\NSImport-%VER%-Setup.exe" || (echo FEHLER beim Signieren & exit /b 1)

REM ---- ZIP ------------------------------------------------------------------
copy /Y "%WURZEL%\installer\Install.bat" "%DIST%\paket\" >nul
copy /Y "%WURZEL%\installer\Uninstall.bat" "%DIST%\paket\" >nul
copy /Y "%WURZEL%\installer\README.txt" "%DIST%\paket\" >nul
if exist "%DIST%\NSImport-%VER%.zip" del "%DIST%\NSImport-%VER%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%DIST%\paket\*' -DestinationPath '%DIST%\NSImport-%VER%.zip'"
if errorlevel 1 (echo FEHLER: ZIP & exit /b 1)

REM ---- Pruefsummen fuer die Release-Seite -----------------------------------
powershell -NoProfile -Command "Get-ChildItem '%DIST%\NSImport-%VER%*' | ForEach-Object { '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name } | Set-Content -Encoding ascii '%DIST%\SHA256SUMS.txt'"
echo.
echo  FERTIG:
type "%DIST%\SHA256SUMS.txt"
exit /b 0
