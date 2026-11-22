#!/bin/bash
# 从星语图标的 SVG 生成 Windows 用的 .ico（需要 rsvg-convert 和 ImageMagick）。
#   nova/make-icons.sh ../nova-macos/resources/brand
# weasel.ico  程序和安装包图标
# zh.ico      托盘：中文状态
# en.ico      托盘：英文状态（图形变浅灰）
set -euo pipefail
src=${1:?用法: nova/make-icons.sh <nova-macos/resources/brand>}
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

sizes=(16 24 32 48 64 256)
color=() gray=()
for s in "${sizes[@]}"; do
  rsvg-convert -w "$s" -h "$s" "$src/xingyu.svg" -o "$tmp/c$s.png"
  magick "$tmp/c$s.png" -colorspace Gray +level 55%,100% -colorspace sRGB "$tmp/g$s.png"
  color+=("$tmp/c$s.png") gray+=("$tmp/g$s.png")
done
magick "${color[@]}" resource/weasel.ico
magick "${color[@]}" resource/zh.ico
magick "${gray[@]}" resource/en.ico
echo "已生成 resource/weasel.ico zh.ico en.ico"
