@echo off
rem Build the Nova Windows binaries. Environment comes from setup.bat; package.sh runs this. Last line BUILD_OK on success. See README.md.
rem
rem   1. copy nova-macos\nova-lm into librime\plugins\nova-lm, build rime.dll (librime + lua + octagram + nova-lm + ggml).
rem      Windows librime cannot load plugin DLLs, so nova-lm is compiled into rime.dll.
rem   2. copy nova-macos\nova-ui into nova-windows\nova-ui; rime headers/libs into include\ lib64\ output\.
rem   3. build weasel: whole x64 solution, then Win32 WeaselTSF (weasel.dll for 32-bit apps) and WeaselSetup.
rem Logs: %NOVA_ROOT%\build-*.log. Keep this file ASCII-only: cmd parses .bat in the console code page.
setlocal EnableExtensions
if not defined NOVA_ROOT set "NOVA_ROOT=C:\nova"
set "R=%NOVA_ROOT%"
call "%~dp0versions.bat"
set "BOOST_ROOT=%R%\deps\boost_%BOOST_VER%"
set "LR=%R%\src\librime"
set "W=%R%\nova-windows"
set "M=%R%\nova-macos"
call :find_vs || exit /b 1

echo [build] == rime.dll
if not exist "%LR%\plugins\nova-lm" mkdir "%LR%\plugins\nova-lm"
copy /y "%M%\nova-lm\nova_lm.cc" "%LR%\plugins\nova-lm\" >nul || goto fail
copy /y "%M%\nova-lm\tree_lm.h" "%LR%\plugins\nova-lm\" >nul || goto fail
copy /y "%M%\nova-lm\build-win.cmake" "%LR%\plugins\nova-lm\CMakeLists.txt" >nul || goto fail
call :librime || goto fail

echo [build] == weasel x64
if not exist "%W%\nova-ui\src" mkdir "%W%\nova-ui\src"
if not exist "%W%\nova-ui\include" mkdir "%W%\nova-ui\include"
copy /y "%M%\nova-ui\src\*.cc" "%W%\nova-ui\src\" >nul || goto fail
copy /y "%M%\nova-ui\src\*.h" "%W%\nova-ui\src\" >nul 2>&1
copy /y "%M%\nova-ui\include\*.h" "%W%\nova-ui\include\" >nul || goto fail
if not exist "%W%\lib64" mkdir "%W%\lib64"
copy /y "%LR%\dist\include\rime_*.h" "%W%\include\" >nul || goto fail
copy /y "%LR%\dist\lib\rime.lib" "%W%\lib64\" >nul || goto fail
copy /y "%LR%\dist\lib\rime.dll" "%W%\output\" >nul || goto fail
rem Build Tools has no MFC; the .rc files only need afxres.h for winres.h, so use a stub
copy /y "%~dp0inc\afxres.h" "%W%\include\afxres.h" >nul || goto fail
call :weasel64 || goto fail

echo [build] == weasel Win32
call :weasel32 || goto fail

for %%f in (weasel.dll weaselx64.dll WeaselServer.exe WeaselDeployer.exe WeaselSetup.exe rime.dll) do (
  if not exist "%W%\output\%%f" (echo [build] missing output\%%f & goto fail)
)
echo [build] BUILD_OK
exit /b 0

:fail
echo [build] BUILD_FAILED
exit /b 1

rem ---------------------------------------------------------------------------
:find_vs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo [build] Visual Studio not found. & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo [build] VS C++ tools not found. & exit /b 1)
set "VCVARS=%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"
set "CMAKEBIN=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJABIN=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
exit /b 0

:librime
setlocal
call "%VCVARS%" x64 >nul || exit /b 1
set "PATH=%CMAKEBIN%;%NINJABIN%;%PATH%"
set "LRF=%LR:\=/%"
cmake -S "%LR%" -B "%LR%\build" -G Ninja -DCMAKE_BUILD_TYPE:STRING=Release "-DCMAKE_USER_MAKE_RULES_OVERRIDE:PATH=%LRF%/cmake/c_flag_overrides.cmake" "-DCMAKE_USER_MAKE_RULES_OVERRIDE_CXX:PATH=%LRF%/cmake/cxx_flag_overrides.cmake" -DCMAKE_EXE_LINKER_FLAGS_INIT:STRING=-llibcmt -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DBUILD_STATIC=ON -DBUILD_SHARED_LIBS=ON -DBUILD_TEST=OFF -DENABLE_LOGGING=ON "-DCMAKE_PREFIX_PATH:PATH=%LRF%" "-DCMAKE_INSTALL_PREFIX:PATH=%LRF%/dist" "-DNOVA_GGML_DIR:PATH=%R:\=/%/ggml-install" > "%R%\build-librime.log" 2>&1 || (type "%R%\build-librime.log" & exit /b 1)
cmake --build "%LR%\build" --target install -j 8 >> "%R%\build-librime.log" 2>&1 || (type "%R%\build-librime.log" & exit /b 1)
findstr /c:"rime_plugins_modules: lua;nova_lm;octagram" "%R%\build-librime.log" >nul || (echo [build] librime plugins missing, see build-librime.log & exit /b 1)
endlocal
exit /b 0

:weasel64
setlocal
call "%VCVARS%" x64 >nul || exit /b 1
set "INCLUDE=%~dp0inc;%INCLUDE%"
cd /d "%W%"
cscript //nologo render.js weasel.props BOOST_ROOT PLATFORM_TOOLSET VERSION_MAJOR VERSION_MINOR VERSION_PATCH PRODUCT_VERSION FILE_VERSION || exit /b 1
msbuild weasel.sln /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:WindowsTargetPlatformVersion=%SDKVER% /nologo /v:minimal > "%R%\build-weasel64.log" 2>&1 || (findstr /i "error" "%R%\build-weasel64.log" & exit /b 1)
endlocal
exit /b 0

:weasel32
setlocal
call "%VCVARS%" x86 >nul || exit /b 1
set "INCLUDE=%~dp0inc;%INCLUDE%"
cd /d "%W%"
msbuild WeaselTSF\WeaselTSF.vcxproj /m /t:Build /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=%W%\ /p:WindowsTargetPlatformVersion=%SDKVER% /nologo /v:minimal > "%R%\build-weasel32.log" 2>&1 || (findstr /i "error" "%R%\build-weasel32.log" & exit /b 1)
msbuild WeaselSetup\WeaselSetup.vcxproj /m /t:Build /p:Configuration=Release /p:Platform=Win32 /p:SolutionDir=%W%\ /p:WindowsTargetPlatformVersion=%SDKVER% /nologo /v:minimal >> "%R%\build-weasel32.log" 2>&1 || (findstr /i "error" "%R%\build-weasel32.log" & exit /b 1)
endlocal
exit /b 0
