@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ============================================================
echo  REXT-PROXY - Terraria 1.4.5.8 / Protocol 326
echo  Windows C++17 build (reliable compiler settings)
echo ============================================================
echo.

where g++ >nul 2>nul
if errorlevel 1 (
  echo [ERROR] g++ was not found.
  echo Install MSYS2 with the MinGW-w64 GCC toolchain, then add its mingw64\bin folder to PATH.
  echo Example toolchain: C:\msys64\mingw64\bin
  pause
  exit /b 1
)

echo [BUILD] Compiling proxy sources...
g++ -std=c++17 -O0 -DNDEBUG -Wall -Wextra -Wpedantic main.cpp ProxyServer.cpp WarpManager.cpp CommandHandler.cpp PlayerRules.cpp -o dimensions.exe -lws2_32 -Wl,--export-all-symbols -Wl,--out-implib,libdimensions.dll.a
if errorlevel 1 (
  echo.
  echo [ERROR] Build failed. Review the compiler output above.
  pause
  exit /b 1
)

if not exist extensions mkdir extensions
g++ -std=c++17 -shared -O0 extensions\PlayTime\PlayTimeExtension.cpp -o extensions\PlayTime.dll -L. -ldimensions -lws2_32
if errorlevel 1 (
  echo [ERROR] PlayTime extension build failed.
  pause
  exit /b 1
)
if not exist logs mkdir logs
if not exist extensions mkdir extensions

echo.
echo [OK] dimensions.exe has been built successfully.
echo [NEXT] Check config.json, then run run.bat or: dimensions.exe config.json
echo [LOGS] logs\dimensions-YYYY-MM-DD.log and logs\packets-YYYY-MM-DD.log
echo.
endlocal
