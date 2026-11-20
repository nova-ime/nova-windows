@echo off
rem build the offline Nova panel renderer (x64 MSVC); output C:\nova\weasel\nova-shot\build\nova_shot.exe
call C:\nova\env.bat
set BOOST_ROOT=C:\nova\deps\boost_1_84_0
cd /d C:\nova\weasel\nova-shot
if not exist build mkdir build
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE /I..\include /I..\nova-ui\include /I%BOOST_ROOT% /Fobuild\ nova_shot.cpp ..\WeaselUI\NovaRenderer.cpp ..\nova-ui\src\model.cc ..\nova-ui\src\layout.cc /Febuild\nova_shot.exe /link d2d1.lib dwrite.lib windowscodecs.lib ole32.lib user32.lib
echo BUILD_EXIT=%errorlevel%
