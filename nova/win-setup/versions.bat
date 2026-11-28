@echo off
rem Every external dependency of the Nova Windows build, pinned. Read by setup.bat and build.bat.
rem To upgrade one: change its version/commit and sha256 here, delete its directory under %NOVA_ROOT%, rerun setup.bat.

rem 7-Zip standalone extractor (Windows tar drops files from the boost .7z)
set "SEVENZR_URL=https://github.com/ip7z/7zip/releases/download/24.09/7zr.exe"
set "SEVENZR_SHA256=d2c0045523cf053a6b43f9315e9672fc2535f06aeadd4ffa53c729cd8b2b6dfe"

rem Boost: headers plus a few static libs (weasel)
set "BOOST_VER=1_84_0"
set "BOOST_URL=https://archives.boost.io/release/1.84.0/source/boost_1_84_0.7z"
set "BOOST_SHA256=81a4d10075731966477276c47324ecd9cac02da49899d36c48eba66e40014f25"

rem librime 1.16.0 with the official prebuilt deps
set "LIBRIME_URL=https://github.com/rime/librime.git"
set "LIBRIME_SHA=a251145d3aafa33871824a40bbec04c966bd8b56"
set "RIMEDEPS_URL=https://github.com/rime/librime/releases/download/1.16.0/rime-deps-a251145-Windows-msvc-x64.7z"
set "RIMEDEPS_SHA256=8ddfa46881a8f20c37fbbfdc3b9d0e9c5f7958f486cce4335a18be6ea8277af2"

rem librime plugins: default branch as of 2026-11-19 (what 1.0.3 shipped with)
set "LUA_URL=https://github.com/hchunhui/librime-lua.git"
set "LUA_SHA=6da5d6cedb853380ed1b5b9b6979cb1a9c028353"
set "LUA_THIRDPARTY_SHA=9e5bb71db1544913f8005dadc3df8439c00d08b7"
set "OCTAGRAM_URL=https://github.com/lotem/librime-octagram.git"
set "OCTAGRAM_SHA=57d18b9f58e5284bd891d559f6bdd16cf60341e9"

rem ggml v1.0.0 (GPT-2 inference)
set "GGML_URL=https://github.com/ggml-org/ggml.git"
set "GGML_SHA=417a024b4fb6bc3116d41af7c067166a039bdf12"

rem Nova repos: tip of branch nova; package.sh copies the Mac working copy over them before each build
set "NOVA_WINDOWS_URL=https://github.com/nova-ime/nova-windows.git"
set "NOVA_MACOS_URL=https://github.com/nova-ime/nova-macos.git"
set "NOVA_BRANCH=nova"

rem weasel version (file properties) and Windows SDK
set "VERSION_MAJOR=0"
set "VERSION_MINOR=17"
set "VERSION_PATCH=4"
set "PRODUCT_VERSION=0.17.4.0"
set "FILE_VERSION=0.17.4.0"
set "SDKVER=10.0.22621.0"
set "PLATFORM_TOOLSET=v143"
set "BJAM_TOOLSET=msvc-14.3"
