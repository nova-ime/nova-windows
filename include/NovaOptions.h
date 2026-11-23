#pragma once
// Nova 的输入方式和两个开关。存在 HKCU\Software\Rime\Weasel 下，安装器写初值，
// 托盘菜单和语言栏菜单改它；WeaselServer 每建一个会话就把它设给 Rime（RimeWithWeasel.cpp）。
// 对应 Mac 版 sources/NovaSettings.swift 里的 NovaOptions。
//
// 输入方式是 nova.schema.yaml 里那组单选开关：zrm flypy mspy sogou quanpin，顺序一致。
#include <windows.h>
#include <string>
#include <resource.h>
#include <WeaselConstants.h>

namespace nova {

struct Layout {
  UINT menu_id;
  const char* option;
  const wchar_t* key;
};

inline const Layout kLayouts[] = {
    {ID_NOVA_LAYOUT_ZRM, "zrm", L"zrm"},
    {ID_NOVA_LAYOUT_FLYPY, "flypy", L"flypy"},
    {ID_NOVA_LAYOUT_MSPY, "mspy", L"mspy"},
    {ID_NOVA_LAYOUT_SOGOU, "sogou", L"sogou"},
    {ID_NOVA_LAYOUT_QUANPIN, "quanpin", L"quanpin"},
};

struct Toggle {
  UINT menu_id;
  const char* option;
  const wchar_t* value_name;
};

inline const Toggle kToggles[] = {
    {ID_NOVA_FULL_SHAPE, "full_shape", L"NovaFullShape"},
    {ID_NOVA_ASCII_PUNCT, "ascii_punct", L"NovaAsciiPunct"},
};

// 没设过（或值不认识）就是自然码。
inline std::wstring layout() {
  wchar_t buf[32] = {0};
  DWORD len = sizeof(buf);
  if (RegGetValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, L"NovaLayout",
                   RRF_RT_REG_SZ, NULL, buf, &len) == ERROR_SUCCESS) {
    for (const auto& l : kLayouts)
      if (buf == std::wstring(l.key))
        return buf;
  }
  return L"zrm";
}

inline void set_layout(const wchar_t* key) {
  RegSetKeyValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, L"NovaLayout", REG_SZ,
                  key, (DWORD)((wcslen(key) + 1) * sizeof(wchar_t)));
}

inline bool toggle(const Toggle& t) {
  DWORD v = 0, len = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, t.value_name,
                   RRF_RT_REG_DWORD, NULL, &v, &len) != ERROR_SUCCESS)
    return false;
  return v != 0;
}

inline void set_toggle(const Toggle& t, bool on) {
  DWORD v = on ? 1 : 0;
  RegSetKeyValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, t.value_name, REG_DWORD,
                  &v, sizeof(v));
}

// 注册表里的字符串值，没有就是空串。
inline std::wstring reg_string(const wchar_t* name) {
  wchar_t buf[256] = {0};
  DWORD len = sizeof(buf);
  if (RegGetValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, name, RRF_RT_REG_SZ,
                   NULL, buf, &len) == ERROR_SUCCESS)
    return buf;
  return L"";
}

// 空串就删掉这个值。
inline void set_reg_string(const wchar_t* name, const std::wstring& value) {
  if (value.empty()) {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, name);
    return;
  }
  RegSetKeyValueW(HKEY_CURRENT_USER, WEASEL_REG_KEY, name, REG_SZ,
                  value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
}

// 辅助码：zrm flypy moqi，存在 NovaAux，没设过就是自然码。换辅助码要重新编译词库，
// 由设置窗口（WeaselDeployer /nova，见 WeaselDeployer/NovaAux.cpp）来做，这里只读。
inline std::wstring aux() {
  const std::wstring v = reg_string(L"NovaAux");
  return (v == L"flypy" || v == L"moqi") ? v : L"zrm";
}

// 菜单命令是 Nova 的选项就改注册表并返回 true。
inline bool handle_menu(UINT id) {
  for (const auto& l : kLayouts)
    if (l.menu_id == id) {
      set_layout(l.key);
      return true;
    }
  for (const auto& t : kToggles)
    if (t.menu_id == id) {
      set_toggle(t, !toggle(t));
      return true;
    }
  return false;
}

// 弹出菜单前按注册表打勾。CheckMenuItem 按命令号找，子菜单里的也找得到。
inline void check_menu(HMENU menu) {
  const std::wstring cur = layout();
  UINT cur_id = ID_NOVA_LAYOUT_ZRM;
  for (const auto& l : kLayouts)
    if (cur == l.key)
      cur_id = l.menu_id;
  CheckMenuRadioItem(menu, ID_NOVA_LAYOUT_ZRM, ID_NOVA_LAYOUT_QUANPIN, cur_id,
                     MF_BYCOMMAND);
  for (const auto& t : kToggles)
    CheckMenuItem(menu, t.menu_id,
                  MF_BYCOMMAND | (toggle(t) ? MF_CHECKED : MF_UNCHECKED));
}

}  // namespace nova
