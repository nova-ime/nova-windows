#pragma once
// 按某种辅助码生成 nova_wx.dict.yaml 和 nova_wx_jp.dict.yaml。
// 移植自 Mac 版 sources/NovaAuxGen.swift，规则见 nova-macos/scripts/wanxiang/aux_tables.py，三边输出逐字节一致。
// 只用标准库，可以在 Mac 上单独编译测试。
#include <filesystem>
#include <functional>
#include <string>

namespace nova_aux {

// base：不带形码的词条表（词\t音节连写\t词频），aux：「字 → 形码」表（字\t形码[,形码...]），
// label：写进文件头的辅助码名字（UTF-8）。progress 收到 0…1。失败时 error 写打不开的文件名。
bool generate(const std::filesystem::path& base_path,
              const std::filesystem::path& aux_path,
              const std::string& label,
              const std::filesystem::path& main_path,
              const std::filesystem::path& jp_path,
              const std::function<void(double)>& progress,
              std::string* error);

}  // namespace nova_aux
