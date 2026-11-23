#pragma once
// 换辅助码：在本机重新生成并编译词库。对应 Mac 版 sources/NovaAuxDict.swift。
//
// 预编译的词库（data\build）用自然码形码。万象各辅助码版本的词条完全一样，形码只跟字有关，
// 所以安装包只带一份不含形码的词条表（data\nova_aux\nova_wx.base.txt）和每种形码一张「字 → 形码」表。
// 用户选了别的形码时：
//   1. 生成 nova_wx.dict.yaml、nova_wx_jp.dict.yaml，写进用户目录（NovaAuxGen.cpp）。
//   2. 用安装目录里的 rime_deployer.exe 编译到临时目录，读它的日志显示进度。
//   3. 让 WeaselServer 进入维护模式（放开它映射着的词表），把编好的词表换进用户目录的 build\，
//      删掉生成的源文件（约 190 MB），重新部署，再恢复服务。
//      librime 找不到源文件时直接用 build 里的词表，只重建拼写索引。
// 换回自然码时删掉用户目录里的词表，回到预编译的那份。
//
// Windows 上被映射着的文件不能覆盖也不能删，所以换文件必须在维护模式里做；编译在临时目录，不用停服务。
#include <chrono>
#include <functional>
#include <mutex>
#include <string>

namespace nova_aux {

struct Scheme {
  const wchar_t* id;
  const wchar_t* name;   // 设置窗口里的名字
  const char* label;     // 生成文件头里的名字（UTF-8）
};

constexpr int kSchemeCount = 3;
extern const Scheme kSchemes[kSchemeCount];
constexpr int kStageCount = 5;
extern const wchar_t* const kStages[kStageCount];

// 注册表里记的辅助码（zrm flypy moqi），没设过就是自然码。
std::wstring current();
std::wstring scheme_name(const std::wstring& id);

// 用的不是自然码，而升级后词条表变了：用户目录里的词表过时了，要重新生成。
bool needs_rebuild();

// 部署前调用：要重新生成就同步做完（不显示界面）。返回 false 表示做了但失败了。
bool rebuild_if_needed();

class Builder {
 public:
  // 做完才返回，界面在后台线程里调用。成功后把选择记进注册表。
  bool run(const std::wstring& scheme);
  // 只重新部署（「重新加载设置」）。
  bool reload();

  // 以下给界面读，线程安全。
  bool running() const;
  int stage() const;
  double fraction() const;
  double elapsed() const;
  std::wstring error() const;

 private:
  void begin();
  void end();
  void set(int stage, double within = 0);
  void parse(const std::string& line);
  bool compile(const std::wstring& user,
               const std::wstring& shared,
               const std::wstring& staging);
  // 停服务、换文件、重新部署、恢复服务。swap 返回 false 时仍会部署并恢复服务。
  bool maintain(const std::function<bool()>& swap);
  bool fail(const std::wstring& message);

  mutable std::mutex mutex_;
  bool running_ = false;
  int stage_ = 0;
  double fraction_ = 0;
  std::chrono::steady_clock::time_point began_, finished_;
  std::chrono::steady_clock::time_point read_began_, sort_began_;
  bool reading_ = false, sorting_ = false;
  double sort_expected_ = 20;
  int passes_ = 0;
  std::string tail_;  // 编译日志里最后几行错误（UTF-8）
  std::wstring error_;
};

}  // namespace nova_aux
