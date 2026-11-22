#!/bin/bash
# 在 Mac 上打 Windows 安装包：output/archives/Nova-<版本>-installer.exe。
#
# 程序在一台装好 VS 2022 的 Windows 上编（NOVA_WIN_HOST，ssh 登录；C:\nova 下的编译环境见
# nova-macos/.scratch/win/wbat），Nova 的数据取自 nova-macos 编好的 app，安装包用 Homebrew 的 makensis 打。
#
#   NOVA_VERSION=1.0.1 nova/package.sh            编译、取回、打包
#   NOVA_SKIP_BUILD=1 nova/package.sh             只重新打包（程序用上次取回的）
#
# 步骤：
#   1. 把这个仓库里改过的源码和 nova-macos/nova-ui 拷到 Windows 的 C:\nova\weasel，编 x64 和 Win32。
#   2. 取回 weasel*.dll、Weasel*.exe 和 C:\nova\librime\dist\lib\rime.dll（带 nova-lm，见 rebuild-nova.bat）。
#   3. output/nova-data/：nova-macos 的 SharedSupport（方案、Lua、模型、预编译词库、OpenCC），
#      去掉鼠须管的 squirrel.yaml，加上小狼毫的 weasel.yaml 和使用说明 help.html。
#      词库是在 Mac 上编的；Windows 上用同一份 .bin，200 句逐键回放和 Mac 输出一致。
#   4. makensis output/nova.nsi。
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${NOVA_VERSION:-1.0.1}"
HOST="${NOVA_WIN_HOST:-192.168.1.3}"
WUSER="${NOVA_WIN_USER:-steve chan}"
MACOS="${NOVA_MACOS_DIR:-../nova-macos}"
SS="$MACOS/build-release/Build/Products/Release/Squirrel.app/Contents/SharedSupport"
UPSTREAM_BASE=f96ed24   # 分叉前的小狼毫提交；之后改过的文件都要拷过去

# 用户名带空格，scp 的 user@host 写法不认，所以用临时 ssh 配置给这台机器起个别名
SSHCFG=$(mktemp -t nova-ssh)
trap 'rm -f "$SSHCFG"' EXIT
printf 'Host novawin\n  HostName %s\n  User "%s"\n  BatchMode yes\n' "$HOST" "$WUSER" >"$SSHCFG"
if [ -f ~/.ssh/config ]; then printf 'Include ~/.ssh/config\n' >>"$SSHCFG"; fi
W=novawin
wssh() { ssh -F "$SSHCFG" "$W" "$1" 2>&1 | grep -v '^\*\*' || true; }
wput() { scp -q -F "$SSHCFG" "$@" || { echo "上传失败：$*"; exit 1; }; }
wget_() { scp -q -F "$SSHCFG" "$W:$1" "$2" || { echo "下载失败：$1"; exit 1; }; }

[ -f "$SS/build/nova_wx.table.bin" ] || { echo "没有 nova-macos 编好的数据：$SS。先在 nova-macos 运行 scripts/build-nova.sh。"; exit 1; }

if [ -z "${NOVA_SKIP_BUILD:-}" ]; then
  echo "== 拷源码到 Windows"
  files=$( { git diff --name-only "$UPSTREAM_BASE"; git ls-files --others --exclude-standard; } \
    | grep -E '\.(cpp|h|hpp|rc|vcxproj|filters)$' | sort -u)
  for f in $files; do
    [ -f "$f" ] && wput "$f" "$W:C:/nova/weasel/$f"
  done
  wssh 'if not exist C:\nova\weasel\nova-ui\src mkdir C:\nova\weasel\nova-ui\src C:\nova\weasel\nova-ui\include'
  for f in "$MACOS"/nova-ui/src/*.cc "$MACOS"/nova-ui/src/*.h; do
    [ -f "$f" ] && wput "$f" "$W:C:/nova/weasel/nova-ui/src/"
  done
  wput "$MACOS"/nova-ui/include/*.h "$W:C:/nova/weasel/nova-ui/include/"

  echo "== 编译 x64"
  out=$(wssh 'C:\nova\weasel-build.bat')
  echo "$out" | grep -E "error|MSBUILD_EXIT" | tail -20
  echo "$out" | grep -q "MSBUILD_EXIT=0" || { echo "x64 编译失败"; exit 1; }
  echo "== 编译 Win32（32 位程序里用的 weasel.dll）"
  out=$(wssh 'C:\nova\weasel-win32.bat')
  echo "$out" | grep -E "error|_EXIT" | tail -20
  echo "$out" | grep -q "TSF_EXIT=0" || { echo "Win32 编译失败"; exit 1; }

  echo "== 取回程序"
  for f in weasel.dll weaselx64.dll WeaselServer.exe WeaselDeployer.exe WeaselSetup.exe WeaselLanguageProfile.ps1 \
           start_service.bat stop_service.bat; do
    wget_ "C:/nova/weasel/output/$f" output/
    [ -f "output/$f" ] || { echo "没取到 $f"; exit 1; }
  done
  wget_ "C:/nova/librime/dist/lib/rime.dll" output/
fi
cp LICENSE.txt output/LICENSE.txt

echo "== 准备数据"
D=output/nova-data
rm -rf "$D"
mkdir -p "$D"
rsync -a --exclude squirrel.yaml --exclude installation.yaml --exclude user.yaml "$SS/" "$D/"
cp output/data/weasel.yaml nova/help.html "$D/"
echo "数据 $(du -sh "$D" | cut -f1)"

echo "== 打包"
mkdir -p output/archives
(cd output && makensis -V2 -DNOVA_VERSION="$VERSION" nova.nsi)
ls -la "output/archives/Nova-$VERSION-installer.exe"
