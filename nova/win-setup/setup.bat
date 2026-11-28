@echo off
rem Set up the Nova build environment on a fresh Windows box from nothing, then build once. Safe to rerun.
rem
rem Needs Git for Windows and VS 2022 Build Tools (C++ desktop + ATL + Windows 11 SDK 22621). See README.md.
rem
rem   setup.bat                              install into C:\nova
rem   set NOVA_ROOT=D:\nova                  other directory
rem   set NOVA_PROXY=http://127.0.0.1:7898   download through a proxy (e.g. the Mac via ssh -R)
rem
rem Layout:
rem   dl\            downloaded archives     tools\    7zr.exe, user-config.jam for b2
rem   deps\boost_*   boost (x64 and x86 static libs)
rem   src\ggml  src\librime  src\librime-lua  src\librime-octagram
rem   ggml-install\  ggml static libs
rem   nova-windows\  this repo (weasel fork)  nova-macos\  nova-lm and nova-ui sources
setlocal EnableExtensions
if not defined NOVA_ROOT set "NOVA_ROOT=C:\nova"
set "R=%NOVA_ROOT%"
call "%~dp0versions.bat"

set "CURL=curl -fsSL --ssl-no-revoke --retry 3"
set "GIT=git -c http.sslBackend=openssl -c core.autocrlf=false"
if defined NOVA_PROXY (
  set "CURL=%CURL% -x %NOVA_PROXY%"
  set "GIT=%GIT% -c http.proxy=%NOVA_PROXY%"
)

where git >nul 2>&1 || (echo [setup] git not found. Install Git for Windows first. & exit /b 1)
call :find_vs || exit /b 1
for %%d in (dl tools deps src) do if not exist "%R%\%%d" mkdir "%R%\%%d"

echo [setup] == 7zr
call :fetch "%SEVENZR_URL%" "%R%\tools\7zr.exe" %SEVENZR_SHA256% || goto fail
set "SZ=%R%\tools\7zr.exe"

echo [setup] == boost %BOOST_VER%
set "BOOST_ROOT=%R%\deps\boost_%BOOST_VER%"
if not exist "%BOOST_ROOT%\boost\version.hpp" (
  call :fetch "%BOOST_URL%" "%R%\dl\boost_%BOOST_VER%.7z" %BOOST_SHA256% || goto fail
  "%SZ%" x -y -bd -o"%R%\deps" "%R%\dl\boost_%BOOST_VER%.7z" >nul || goto fail
)
rem b2 cannot find vcvarsall by itself ("don't know how to make ...vcvarsall.bat"), skips everything and still
rem exits 0. So enter the VS environment first and give b2 a setup script that does nothing.
> "%R%\tools\nosetup.bat" echo @exit /b 0
> "%R%\tools\user-config.jam" echo using msvc : 14.3 : cl.exe : ^<setup^>%R:\=/%/tools/nosetup.bat ;
call :boost x64 64 || goto fail
call :boost x86 32 || goto fail

echo [setup] == sources
call :clone "%GGML_URL%" "%R%\src\ggml" %GGML_SHA% || goto fail
call :clone "%LIBRIME_URL%" "%R%\src\librime" %LIBRIME_SHA% || goto fail
call :clone "%LUA_URL%" "%R%\src\librime-lua" %LUA_SHA% || goto fail
call :clone "%LUA_URL%" "%R%\src\librime-lua\thirdparty" %LUA_THIRDPARTY_SHA% || goto fail
call :clone "%OCTAGRAM_URL%" "%R%\src\librime-octagram" %OCTAGRAM_SHA% || goto fail
if not exist "%R%\nova-windows\.git" (
  %GIT% clone -q --depth 1 -b %NOVA_BRANCH% "%NOVA_WINDOWS_URL%" "%R%\nova-windows" || goto fail
  %GIT% -C "%R%\nova-windows" submodule update -q --init --depth 1 plum || goto fail
)
if not exist "%R%\nova-macos\.git" (
  %GIT% clone -q --depth 1 -b %NOVA_BRANCH% "%NOVA_MACOS_URL%" "%R%\nova-macos" || goto fail
)

echo [setup] == librime deps
if not exist "%R%\src\librime\.nova-deps-ok" (
  call :fetch "%RIMEDEPS_URL%" "%R%\dl\rime-deps.7z" %RIMEDEPS_SHA256% || goto fail
  "%SZ%" x -y -bd -o"%R%\src\librime" "%R%\dl\rime-deps.7z" >nul || goto fail
  > "%R%\src\librime\.nova-deps-ok" echo %RIMEDEPS_SHA256%
)
if not exist "%R%\src\librime\plugins\lua" mklink /j "%R%\src\librime\plugins\lua" "%R%\src\librime-lua" >nul || goto fail
if not exist "%R%\src\librime\plugins\octagram" mklink /j "%R%\src\librime\plugins\octagram" "%R%\src\librime-octagram" >nul || goto fail

echo [setup] == ggml
if not exist "%R%\ggml-install\lib\ggml-cpu.lib" call :ggml || goto fail
if not exist "%R%\ggml-install\lib\ggml-cpu.lib" goto fail

echo [setup] == build
call "%~dp0build.bat" || goto fail
echo [setup] SETUP_OK
exit /b 0

:fail
echo [setup] SETUP_FAILED
exit /b 1

rem ---------------------------------------------------------------------------
:find_vs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo [setup] Visual Studio not found. & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo [setup] VS C++ tools not found. & exit /b 1)
set "VCVARS=%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"
set "CMAKEBIN=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJABIN=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if not exist "%VSDIR%\VC\Tools\MSVC" (echo [setup] MSVC missing in "%VSDIR%" & exit /b 1)
exit /b 0

rem :fetch <url> <file> <sha256>   download and verify; skip if the file is there and matches
:fetch
if exist "%~2" (
  certutil -hashfile "%~2" SHA256 | findstr /i /x "%~3" >nul && exit /b 0
  del /q "%~2"
)
%CURL% -o "%~2" "%~1" || (echo [setup] download failed: %~1 & exit /b 1)
certutil -hashfile "%~2" SHA256 | findstr /i /x "%~3" >nul || (echo [setup] sha256 mismatch: %~2 & del /q "%~2" & exit /b 1)
exit /b 0

rem :clone <url> <dir> <commit>   fetch only this commit
:clone
if exist "%~2\.git" (
  for /f %%h in ('git -C "%~2" rev-parse HEAD') do if /i "%%h"=="%~3" exit /b 0
  echo [setup] %~2 is not at %~3. Delete it and rerun.
  exit /b 1
)
git init -q "%~2" || exit /b 1
git -C "%~2" remote add origin "%~1"
%GIT% -C "%~2" fetch -q --depth 1 origin %~3 || (echo [setup] fetch failed: %~1 %~3 & exit /b 1)
git -C "%~2" checkout -q FETCH_HEAD || exit /b 1
exit /b 0

rem :boost <vcvars arch> <address-model>   log in %NOVA_ROOT%\boost-<arch>.log
:boost
set "BLIB=%BOOST_ROOT%\stage\lib\libboost_wserialization-vc143-mt-s-x%~2-1_84.lib"
if exist "%BLIB%" exit /b 0
setlocal
call "%VCVARS%" %~1 >nul || exit /b 1
cd /d "%BOOST_ROOT%"
if not exist b2.exe call bootstrap.bat >nul || exit /b 1
b2 --user-config="%R%\tools\user-config.jam" -j8 --with-filesystem --with-json --with-locale --with-regex --with-serialization --with-system --with-thread define=BOOST_USE_WINAPI_VERSION=0x0603 toolset=%BJAM_TOOLSET% link=static runtime-link=static --build-type=complete architecture=x86 address-model=%~2 stage > "%R%\boost-%~1.log" 2>&1
endlocal
if not exist "%BLIB%" (echo [setup] boost %~1 build failed, see %R%\boost-%~1.log & exit /b 1)
exit /b 0

rem :ggml   a subroutine because %PATH% inside a ( ) block expands before vcvarsall runs
:ggml
setlocal
call "%VCVARS%" x64 >nul || exit /b 1
set "PATH=%CMAKEBIN%;%NINJABIN%;%PATH%"
rem GGML_LLAMAFILE: ~12%% faster GPT-2 on x86. GGML_CPU_REPACK has no Q8_0 layout on x86; tree_lm.h uses it on ARM only.
cmake -S "%R%\src\ggml" -B "%R%\src\ggml\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DBUILD_SHARED_LIBS=OFF -DGGML_BACKEND_DL=OFF -DGGML_NATIVE=OFF -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_AVX=ON -DGGML_AVX512=OFF -DGGML_OPENMP=OFF -DGGML_CPU_REPACK=ON -DGGML_LLAMAFILE=ON -DGGML_BUILD_TESTS=OFF -DGGML_BUILD_EXAMPLES=OFF "-DCMAKE_INSTALL_PREFIX=%R:\=/%/ggml-install" > "%R%\ggml.log" 2>&1 || (type "%R%\ggml.log" & exit /b 1)
cmake --build "%R%\src\ggml\build" -j 8 >> "%R%\ggml.log" 2>&1 || (type "%R%\ggml.log" & exit /b 1)
cmake --install "%R%\src\ggml\build" >> "%R%\ggml.log" 2>&1 || exit /b 1
endlocal
exit /b 0
