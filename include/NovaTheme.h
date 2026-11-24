#pragma once
// Nova 星语的主题选择和读取，候选面板（WeaselUI，跑在各个应用的进程里）和设置窗口
// （WeaselDeployer /nova）共用。对应 Mac 版 sources/NovaTheme.swift 里的 NovaThemes。
//
// 选择存在 HKCU\Software\Rime\Weasel：
//   NovaTheme       主题编号，或 "auto"（跟随系统，默认）
//   NovaThemeLight  跟随系统时浅色用哪套（默认 light）
//   NovaThemeDark   跟随系统时深色用哪套（默认 dark）
// 主题文件：安装目录 data\nova_themes\<编号>.yaml 是自带的，用户目录 nova_themes\<编号>.yaml
// 是用户的，同名时用户的优先。读写主题本身用 nova-ui 的 nova_ui_theme_*。
//
// 这里不用 WeaselUtility 里的路径函数：面板在 weasel.dll 里，跑在别的应用的进程中，
// GetModuleFileName(NULL) 拿到的是那个应用的路径。安装目录从 HKLM 的 WeaselRoot 读。
#include <windows.h>
#include <nova_ui.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

// Win10 SDK 只在 _WIN32_WINNT >= 0x0A00 时定义它；项目目标版本更低，值本身各版本都认。
#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif

namespace nova_theme {

namespace fs = std::filesystem;

constexpr wchar_t kRegKey[] = L"Software\\Rime\\Weasel";
constexpr wchar_t kAuto[] = L"auto";

inline std::wstring reg_string(HKEY root, const wchar_t* name, DWORD flags = 0) {
  wchar_t buf[MAX_PATH] = {0};
  DWORD len = sizeof(buf);
  if (RegGetValueW(root, kRegKey, name, RRF_RT_REG_SZ | flags, NULL, buf,
                   &len) == ERROR_SUCCESS)
    return buf;
  return L"";
}

inline void set_reg_string(const wchar_t* name, const std::wstring& value) {
  RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_SZ, value.c_str(),
                  (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

inline std::wstring selection() {
  std::wstring v = reg_string(HKEY_CURRENT_USER, L"NovaTheme");
  return v.empty() ? kAuto : v;
}
inline std::wstring light_id() {
  std::wstring v = reg_string(HKEY_CURRENT_USER, L"NovaThemeLight");
  return v.empty() ? L"light" : v;
}
inline std::wstring dark_id() {
  std::wstring v = reg_string(HKEY_CURRENT_USER, L"NovaThemeDark");
  return v.empty() ? L"dark" : v;
}
inline void set_selection(const std::wstring& id) {
  set_reg_string(L"NovaTheme", id);
}
inline void set_light_id(const std::wstring& id) {
  set_reg_string(L"NovaThemeLight", id);
}
inline void set_dark_id(const std::wstring& id) {
  set_reg_string(L"NovaThemeDark", id);
}

// 系统的「应用模式」是不是深色。读不到（Win10 1809 以前）就当浅色。
inline bool system_dark() {
  DWORD v = 1, len = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\"
                   L"Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v,
                   &len) != ERROR_SUCCESS)
    return false;
  return v == 0;
}

// 现在该用的主题编号。
inline std::wstring current_id() {
  const std::wstring sel = selection();
  if (sel != kAuto)
    return sel;
  return system_dark() ? dark_id() : light_id();
}

inline fs::path shared_dir() {
  // 32 位应用里的 weasel.dll 也要读 64 位注册表视图里的 WeaselRoot。
  std::wstring root =
      reg_string(HKEY_LOCAL_MACHINE, L"WeaselRoot", RRF_SUBKEY_WOW6464KEY);
  if (root.empty())
    root = reg_string(HKEY_LOCAL_MACHINE, L"WeaselRoot");
  if (root.empty())
    return fs::path();
  return fs::path(root) / L"data" / L"nova_themes";
}

inline fs::path user_dir() {
  std::wstring dir = reg_string(HKEY_CURRENT_USER, L"RimeUserDir");
  if (dir.empty()) {
    wchar_t buf[MAX_PATH] = {0};
    ExpandEnvironmentStringsW(L"%AppData%\\Rime", buf, MAX_PATH);
    dir = buf;
  }
  return fs::path(dir) / L"nova_themes";
}

inline std::string utf8(const fs::path& p) {
  auto u8 = p.u8string();
  return std::string(u8.begin(), u8.end());
}

inline std::string utf8(const std::wstring& w) {
  if (w.empty())
    return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr,
                      nullptr);
  return s;
}

inline std::wstring wide(const char* s) {
  if (!s || !*s)
    return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 1)
    return std::wstring();
  std::wstring w(n - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
  return w;
}

// 编号对应的文件：用户目录优先，其次自带。都没有就返回空。
inline fs::path path_of(const std::wstring& id) {
  std::error_code ec;
  for (const fs::path& dir : {user_dir(), shared_dir()}) {
    if (dir.empty())
      continue;
    fs::path p = dir / (id + L".yaml");
    if (fs::is_regular_file(p, ec))
      return p;
  }
  return fs::path();
}

struct ThemeDeleter {
  void operator()(NovaUITheme* t) const { nova_ui_theme_free(t); }
};
using ThemePtr = std::shared_ptr<NovaUITheme>;

inline ThemePtr wrap(NovaUITheme* t) {
  return ThemePtr(t ? t : nova_ui_theme_new(), ThemeDeleter());
}

// 读编号对应的主题；找不到或读不了（比如沙盒应用读不到用户目录）就用默认主题。
inline ThemePtr load(const std::wstring& id) {
  fs::path p = path_of(id);
  return wrap(p.empty() ? nullptr : nova_ui_theme_load(utf8(p).c_str()));
}

// 面板用：当前主题。最多每秒重新看一次注册表和文件修改时间，
// 这样换主题、改主题文件、系统换深浅色都不用重启应用。
inline ThemePtr current() {
  static std::mutex mutex;
  static ThemePtr cached;
  static fs::path cached_path;
  static fs::file_time_type cached_time;
  static ULONGLONG checked = 0;
  std::lock_guard<std::mutex> lock(mutex);
  const ULONGLONG now = GetTickCount64();
  if (cached && now - checked < 1000)
    return cached;
  checked = now;
  fs::path p = path_of(current_id());
  std::error_code ec;
  fs::file_time_type t{};
  if (!p.empty())
    t = fs::last_write_time(p, ec);
  if (cached && p == cached_path && t == cached_time)
    return cached;
  cached = wrap(p.empty() ? nullptr : nova_ui_theme_load(utf8(p).c_str()));
  cached_path = p;
  cached_time = t;
  return cached;
}

}  // namespace nova_theme
