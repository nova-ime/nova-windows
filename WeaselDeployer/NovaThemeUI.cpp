#include "stdafx.h"
#include "NovaThemeUI.h"

#include <NovaTheme.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>
#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../WeaselUI/NovaRenderer.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace nova_theme_ui {
namespace {

constexpr wchar_t kPreviewClass[] = L"NovaThemePreview";
constexpr wchar_t kEditorClass[] = L"NovaThemeEditor";
constexpr wchar_t kFieldsClass[] = L"NovaThemeFields";
// 编辑器存了主题以后通知「外观」页重新读列表。
constexpr UINT WM_NOVA_THEMES_CHANGED = WM_APP + 21;

enum : int {
  IDC_THEME = 300,
  IDC_LIGHT = 301,
  IDC_DARK = 302,
  IDC_EDIT = 310,
  IDC_DUPLICATE = 311,
  IDC_REMOVE = 312,
  IDC_IMPORT = 313,
  IDC_EXPORT = 314,
  // 编辑器
  IDC_SAVE = 320,
  IDC_CANCEL = 321,
  IDC_FIELD = 400,  // 400 + 字段序号 * 2（控件）、+1（「选…」按钮）
};

HINSTANCE g_inst = NULL;

// ---- 共用：D2D / DWrite 工厂 ----

ID2D1Factory* d2d() {
  static ComPtr<ID2D1Factory> f;
  if (!f)
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, f.GetAddressOf());
  return f.Get();
}

IDWriteFactory* dwrite() {
  static ComPtr<IDWriteFactory> f;
  if (!f)
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(f.GetAddressOf()));
  return f.Get();
}

std::wstring get(NovaUITheme* theme, const char* key) {
  return nova_theme::wide(nova_ui_theme_get(theme, key));
}

bool is_dark(NovaUITheme* theme) {
  return get(theme, "appearance") != L"light";
}

std::vector<std::wstring> lines(const char* text) {
  std::vector<std::wstring> out;
  std::wstring all = nova_theme::wide(text);
  size_t start = 0;
  while (start < all.size()) {
    size_t end = all.find(L'\n', start);
    if (end == std::wstring::npos)
      end = all.size();
    if (end > start)
      out.push_back(all.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

// ---- 主题列表 ----

struct Entry {
  std::wstring id, name;
  bool dark = true;
  bool user = false;
  bool overrides_builtin = false;
};

std::vector<Entry> scan(bool with_user) {
  std::vector<Entry> out;
  const fs::path shared = nova_theme::shared_dir();
  const fs::path user = nova_theme::user_dir();
  const std::string shared8 = nova_theme::utf8(shared);
  const std::string user8 = nova_theme::utf8(user);
  NovaUIThemeList* list = nova_ui_theme_list(
      shared8.c_str(), with_user ? user8.c_str() : nullptr);
  if (!list)
    return out;
  int count = 0;
  const NovaUIThemeEntry* entries = nova_ui_theme_list_entries(list, &count);
  for (int i = 0; entries && i < count; ++i) {
    Entry e;
    e.id = nova_theme::wide(entries[i].id);
    e.name = nova_theme::wide(entries[i].name);
    if (e.name.empty())
      e.name = e.id;
    e.dark = std::string(entries[i].appearance ? entries[i].appearance : "") !=
             "light";
    e.user = entries[i].user != 0;
    out.push_back(e);
  }
  nova_ui_theme_list_free(list);
  return out;
}

std::vector<Entry> list_themes() {
  std::set<std::wstring> builtin;
  for (const Entry& e : scan(false))
    builtin.insert(e.id);
  std::vector<Entry> all = scan(true);
  for (Entry& e : all)
    e.overrides_builtin = e.user && builtin.count(e.id);
  return all;
}

// 用户目录里不和已有主题重名的编号。
std::wstring free_id(const std::wstring& base) {
  std::set<std::wstring> taken;
  for (const Entry& e : list_themes())
    taken.insert(e.id);
  std::wstring id = base.empty() ? L"custom" : base;
  const std::wstring stem = id;
  for (int i = 2; taken.count(id); ++i)
    id = stem + L"-" + std::to_wstring(i);
  return id;
}

fs::path user_path(const std::wstring& id) {
  std::error_code ec;
  fs::create_directories(nova_theme::user_dir(), ec);
  return nova_theme::user_dir() / (id + L".yaml");
}

bool save_theme(NovaUITheme* theme, const fs::path& path) {
  return nova_ui_theme_save(theme, nova_theme::utf8(path).c_str()) != 0;
}

// ---- 预览控件：用真正的面板绘制代码画两个样子 ----

// 自然码：wo mf | mk tm | qu | gs yr | sj bu。焦点在最后一块，带备选。
// 和 Mac 版 NovaThemeSample 是同一份内容。
struct Sample {
  NovaUITree* tree = nullptr;
  NovaUIContent tree_content{};
  NovaUICandidate candidates[3] = {};
  NovaUIContent list_content{};

  Sample() {
    const char* text =
        "0\t0\t4\twomf\t\xe6\x88\x91\xe4\xbb\xac\n"                // 我们
        "0c\t4\t8\tmktm\t\xe6\x98\x8e\xe5\xa4\xa9\n"               // 明天
        "0u\t8\t10\tqu\t\xe5\x8e\xbb\n"                            // 去
        "1\t10\t14\tgsyr\t\xe5\x85\xac\xe5\x9b\xad\n"              // 公园
        "0f\t14\t18\tsjbu\t\xe6\x95\xa3\xe6\xad\xa5\x1f" "71\t"    // 散步 71
        "\xe6\x95\xa3\xe5\xb8\x83\x1f" "20\t"                      // 散布 20
        "\xe4\xb8\x89\xe6\xad\xa5\n";                              // 三步
    tree = nova_ui_tree_parse(text);
    int count = 0;
    tree_content.preedit = "";
    tree_content.sel_location = -1;
    tree_content.blocks = nova_ui_tree_blocks(tree, &count);
    tree_content.block_count = count;
    tree_content.letters = "womfmktmqugsyrsjbu";
    tree_content.cursor = 18;

    candidates[0] = {"1", "\xe4\xbd\xa0\xe5\xa5\xbd", ""};    // 你好
    candidates[1] = {"2", "\xe6\x8b\x9f\xe5\xa5\xbd", ""};    // 拟好
    candidates[2] = {"3", "\xe4\xbd\xa0", "n\xc7\x90"};       // 你 nǐ
    list_content.preedit = "nihk";
    list_content.sel_location = -1;
    list_content.letters = "";
    list_content.candidates = candidates;
    list_content.candidate_count = 3;
  }
  ~Sample() { nova_ui_tree_free(tree); }
};

Sample& sample() {
  static Sample s;
  return s;
}

struct Preview {
  nova_theme::ThemePtr theme;
  weasel::NovaRenderer tree, list;
  ComPtr<ID2D1HwndRenderTarget> rt;
  int dpi = 96;
};

Preview* preview_of(HWND hwnd) {
  return reinterpret_cast<Preview*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void set_preview_theme(HWND hwnd, nova_theme::ThemePtr theme) {
  Preview* p = preview_of(hwnd);
  if (!p)
    return;
  p->theme = std::move(theme);
  InvalidateRect(hwnd, NULL, FALSE);
}

void paint_preview(HWND hwnd, Preview* p) {
  PAINTSTRUCT ps;
  BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  const D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);
  if (!p->rt && d2d()) {
    d2d()->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, size), p->rt.GetAddressOf());
  }
  if (p->rt) {
    p->rt->Resize(size);
    const float scale = p->dpi / 96.0f;
    const double width = rc.right / scale;
    weasel::NovaRenderer::Style style;
    style.theme = p->theme ? p->theme : nova_theme::wrap(nullptr);
    const double pad = 14;
    const double max_width = (std::max)(120.0, width - 2 * pad);
    const bool ok_tree = p->tree.BuildContent(
        dwrite(), &sample().tree_content, style, max_width, false);
    const bool ok_list = p->list.BuildContent(
        dwrite(), &sample().list_content, style, max_width, false);

    p->rt->BeginDraw();
    p->rt->SetTransform(D2D1::Matrix3x2F::Identity());
    // 面板浮在别的应用上面，背景按主题的深浅给一个接近的底色。
    const float bg = is_dark(style.theme.get()) ? 0.18f : 0.93f;
    p->rt->Clear(D2D1::ColorF(bg, bg, bg, 1));
    double y = pad;
    if (ok_tree) {
      p->rt->SetTransform(D2D1::Matrix3x2F::Translation(
          (float)(pad * scale), (float)(y * scale)));
      p->tree.Paint(p->rt.Get(), scale, -1);
      y += p->tree.height() + 14;
    }
    if (ok_list) {
      p->rt->SetTransform(D2D1::Matrix3x2F::Translation(
          (float)(pad * scale), (float)(y * scale)));
      p->list.Paint(p->rt.Get(), scale, -1);
    }
    p->rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (p->rt->EndDraw() == D2DERR_RECREATE_TARGET)
      p->rt.Reset();
  }
  EndPaint(hwnd, &ps);
}

LRESULT CALLBACK preview_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NCCREATE: {
      auto* p = new Preview();
      HDC dc = GetDC(NULL);
      p->dpi = GetDeviceCaps(dc, LOGPIXELSY);
      ReleaseDC(NULL, dc);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
      break;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      if (Preview* p = preview_of(hwnd)) {
        paint_preview(hwnd, p);
        return 0;
      }
      break;
    case WM_SIZE:
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;
    case WM_NCDESTROY:
      delete preview_of(hwnd);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- 控件小工具 ----

struct Maker {
  HWND parent;
  const Fonts* fonts;
  int px(int dip) const { return MulDiv(dip, fonts->dpi, 96); }
  HWND make(const wchar_t* cls,
            const wchar_t* text,
            DWORD style,
            int x,
            int y,
            int w,
            int h,
            int id = 0,
            bool bold = false,
            DWORD ex = 0) const {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                             px(x), px(y), px(w), px(h), parent,
                             (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(c, WM_SETFONT,
                 (WPARAM)(bold ? fonts->bold : fonts->normal), TRUE);
    return c;
  }
};

// 文字在给定宽度里折行后的高度（逻辑单位）。
int text_height(const Fonts& fonts, const std::wstring& text, int width) {
  if (text.empty())
    return 0;
  HDC dc = GetDC(NULL);
  HGDIOBJ old = SelectObject(dc, fonts.normal);
  RECT r = {0, 0, MulDiv(width, fonts.dpi, 96), 0};
  DrawTextW(dc, text.c_str(), (int)text.size(), &r,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(dc, old);
  ReleaseDC(NULL, dc);
  return MulDiv(r.bottom, 96, fonts.dpi) + 2;
}

std::wstring choice_name(const std::wstring& choice) {
  if (choice == L"dark")
    return L"深色";
  if (choice == L"light")
    return L"浅色";
  return choice;
}

std::vector<std::wstring> split_choices(const char* choices) {
  std::vector<std::wstring> out;
  std::wstring all = nova_theme::wide(choices);
  size_t start = 0;
  while (start <= all.size()) {
    size_t end = all.find(L',', start);
    if (end == std::wstring::npos)
      end = all.size();
    if (end > start)
      out.push_back(all.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

COLORREF parse_color(const std::wstring& hex) {
  std::wstring d = hex;
  if (!d.empty() && d[0] == L'#')
    d = d.substr(1);
  if (d.size() != 6 && d.size() != 8)
    return RGB(0, 0, 0);
  unsigned long v = wcstoul(d.substr(0, 6).c_str(), nullptr, 16);
  return RGB((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

std::wstring format_color(COLORREF c) {
  wchar_t buf[16];
  swprintf_s(buf, L"#%02x%02x%02x", GetRValue(c), GetGValue(c), GetBValue(c));
  return buf;
}

std::wstring format_number(double v) {
  v = std::round(v * 100) / 100;
  wchar_t buf[32];
  swprintf_s(buf, L"%g", v);
  return buf;
}

// ---- 编辑器 ----

struct FieldRow {
  NovaUIThemeField field;
  std::string key;
  HWND control = NULL;  // 滑块 / 下拉 / 颜色按钮 / 输入框
  HWND value = NULL;    // 数字和颜色旁边显示当前值
  HWND pick = NULL;     // 字体的「选…」
  std::vector<std::wstring> choices;
};

struct Editor {
  HWND hwnd = NULL;
  HWND fields = NULL;  // 左边可滚动的字段列表
  HWND preview = NULL;
  HWND problems = NULL;
  HWND path = NULL;
  HWND notify = NULL;  // 存好以后通知的窗口（设置窗口）
  Fonts fonts;
  nova_theme::ThemePtr theme;
  std::wstring id;
  std::vector<FieldRow> rows;
  std::set<HWND> notes;
  int content_height = 0;  // 字段列表总高，像素
  int scroll = 0;          // 像素
  bool syncing = false;    // 程序在改控件，忽略控件发来的通知
  COLORREF custom[16] = {};
};

Editor* g_editor = nullptr;

int epx(int dip) {
  return MulDiv(dip, g_editor->fonts.dpi, 96);
}

void refresh_problems() {
  Editor& e = *g_editor;
  const auto list = lines(nova_ui_theme_check(e.theme.get()));
  std::wstring text;
  if (list.empty()) {
    text = L"✓ 颜色都看得清。";
  } else {
    for (const auto& line : list)
      text += L"⚠ " + line + L"\r\n";
  }
  SetWindowTextW(e.problems, text.c_str());
  InvalidateRect(e.problems, NULL, TRUE);
}

void sync_row(FieldRow& r) {
  Editor& e = *g_editor;
  const std::wstring v = get(e.theme.get(), r.key.c_str());
  e.syncing = true;
  switch (r.field.kind) {
    case NOVA_THEME_NUMBER: {
      double d = _wtof(v.c_str());
      const double step = r.field.step > 0 ? r.field.step : 1;
      SendMessageW(r.control, TBM_SETPOS, TRUE,
                   (LPARAM)std::lround((d - r.field.min) / step));
      SetWindowTextW(r.value, v.c_str());
      break;
    }
    case NOVA_THEME_CHOICE: {
      int sel = -1;
      for (size_t i = 0; i < r.choices.size(); ++i)
        if (r.choices[i] == v)
          sel = (int)i;
      ComboBox_SetCurSel(r.control, sel);
      break;
    }
    case NOVA_THEME_COLOR:
      SetWindowTextW(r.value, v.c_str());
      InvalidateRect(r.control, NULL, TRUE);
      break;
    default: {
      wchar_t buf[512];
      GetWindowTextW(r.control, buf, 512);
      if (v != buf)
        SetWindowTextW(r.control, v.c_str());
      break;
    }
  }
  e.syncing = false;
}

void set_value(FieldRow& r, const std::wstring& value) {
  Editor& e = *g_editor;
  if (get(e.theme.get(), r.key.c_str()) == value)
    return;
  nova_ui_theme_set(e.theme.get(), r.key.c_str(),
                    nova_theme::utf8(value).c_str());
  sync_row(r);
  set_preview_theme(e.preview, e.theme);
  refresh_problems();
}

void scroll_to(int pos) {
  Editor& e = *g_editor;
  RECT rc;
  GetClientRect(e.fields, &rc);
  const int max_pos = (std::max)(0, e.content_height - (int)rc.bottom);
  pos = (std::max)(0, (std::min)(pos, max_pos));
  const int dy = e.scroll - pos;
  if (dy == 0)
    return;
  e.scroll = pos;
  ScrollWindowEx(e.fields, 0, dy, NULL, NULL, NULL, NULL,
                 SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
  SetScrollPos(e.fields, SB_VERT, pos, TRUE);
}

void update_scrollbar() {
  Editor& e = *g_editor;
  RECT rc;
  GetClientRect(e.fields, &rc);
  SCROLLINFO si = {sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
  si.nMin = 0;
  si.nMax = e.content_height;
  si.nPage = rc.bottom;
  si.nPos = e.scroll;
  SetScrollInfo(e.fields, SB_VERT, &si, TRUE);
}

void build_fields() {
  Editor& e = *g_editor;
  Maker m{e.fields, &e.fonts};
  int count = 0;
  const NovaUIThemeField* fields = nova_ui_theme_fields(&count);
  const int x = 16, label_w = 96, control_x = x + label_w + 10, control_w = 280;
  int y = 12;
  std::string group;
  e.rows.reserve(count);
  for (int i = 0; i < count; ++i) {
    const NovaUIThemeField& f = fields[i];
    if (group != f.group) {
      group = f.group;
      y += group.empty() || i == 0 ? 0 : 10;
      m.make(L"STATIC", nova_theme::wide(f.group).c_str(), SS_LEFT, x, y, 380,
             22, 0, true);
      y += 28;
    }
    FieldRow r;
    r.field = f;
    r.key = f.key;
    const int id = IDC_FIELD + i * 2;
    m.make(L"STATIC", nova_theme::wide(f.label).c_str(), SS_LEFT, x, y + 3,
           label_w, 20);
    switch (f.kind) {
      case NOVA_THEME_NUMBER: {
        r.control = m.make(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS |
                                                     WS_TABSTOP,
                           control_x, y, control_w - 52, 26, id);
        const double step = f.step > 0 ? f.step : 1;
        SendMessageW(r.control, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(r.control, TBM_SETRANGEMAX, TRUE,
                     (LPARAM)std::lround((f.max - f.min) / step));
        r.value = m.make(L"STATIC", L"", SS_RIGHT, control_x + control_w - 48,
                         y + 3, 48, 20);
        break;
      }
      case NOVA_THEME_CHOICE: {
        r.control = m.make(WC_COMBOBOXW, L"",
                           CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                           control_x, y, 160, 200, id);
        r.choices = split_choices(f.choices);
        for (const auto& c : r.choices)
          ComboBox_AddString(r.control, choice_name(c).c_str());
        break;
      }
      case NOVA_THEME_COLOR:
        r.control = m.make(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP,
                           control_x, y, 44, 24, id);
        r.value = m.make(L"STATIC", L"", SS_LEFT, control_x + 54, y + 3, 120,
                         20);
        break;
      default: {
        const bool font = r.key.rfind("font/", 0) == 0;
        r.control = m.make(WC_EDITW, L"", ES_AUTOHSCROLL | WS_TABSTOP,
                           control_x, y, font ? control_w - 60 : control_w,
                           24, id, false, WS_EX_CLIENTEDGE);
        if (font)
          r.pick = m.make(L"BUTTON", L"选…", BS_PUSHBUTTON | WS_TABSTOP,
                          control_x + control_w - 52, y, 52, 24, id + 1);
        break;
      }
    }
    y += 30;
    const std::wstring note = nova_theme::wide(f.note);
    if (!note.empty()) {
      const int h = text_height(e.fonts, note, control_w);
      HWND n = m.make(L"STATIC", note.c_str(), SS_LEFT | SS_NOPREFIX,
                      control_x, y - 4, control_w, h);
      e.notes.insert(n);
      y += h + 2;
    }
    e.rows.push_back(r);
  }
  for (auto& r : e.rows)
    sync_row(r);
  e.content_height = epx(y + 12);
  update_scrollbar();
}

FieldRow* row_of(int id) {
  const int i = (id - IDC_FIELD) / 2;
  if (id < IDC_FIELD || i >= (int)g_editor->rows.size())
    return nullptr;
  return &g_editor->rows[i];
}

void pick_color(FieldRow& r) {
  Editor& e = *g_editor;
  CHOOSECOLORW cc = {sizeof(cc)};
  cc.hwndOwner = e.hwnd;
  cc.rgbResult = parse_color(get(e.theme.get(), r.key.c_str()));
  cc.lpCustColors = e.custom;
  cc.Flags = CC_FULLOPEN | CC_RGBINIT;
  if (ChooseColorW(&cc))
    set_value(r, format_color(cc.rgbResult));
}

void pick_font(FieldRow& r) {
  Editor& e = *g_editor;
  LOGFONTW lf = {};
  const std::wstring cur = get(e.theme.get(), r.key.c_str());
  wcsncpy_s(lf.lfFaceName, cur.substr(0, cur.find(L',')).c_str(),
            LF_FACESIZE - 1);
  CHOOSEFONTW cf = {sizeof(cf)};
  cf.hwndOwner = e.hwnd;
  cf.lpLogFont = &lf;
  cf.Flags = CF_SCREENFONTS | CF_NOSIZESEL | CF_NOSTYLESEL |
             CF_NOVERTFONTS | (cur.empty() ? 0 : CF_INITTOLOGFONTSTRUCT);
  if (ChooseFontW(&cf))
    set_value(r, lf.lfFaceName);
}

void draw_swatch(const DRAWITEMSTRUCT* d) {
  FieldRow* r = row_of((int)d->CtlID);
  if (!r)
    return;
  const COLORREF c = parse_color(get(g_editor->theme.get(), r->key.c_str()));
  RECT rc = d->rcItem;
  FillRect(d->hDC, &rc, GetSysColorBrush(COLOR_BTNFACE));
  DrawEdge(d->hDC, &rc, (d->itemState & ODS_SELECTED) ? EDGE_SUNKEN : EDGE_RAISED,
           BF_RECT);
  InflateRect(&rc, -4, -4);
  HBRUSH b = CreateSolidBrush(c);
  FillRect(d->hDC, &rc, b);
  DeleteObject(b);
  FrameRect(d->hDC, &rc, GetSysColorBrush(COLOR_GRAYTEXT));
  if (d->itemState & ODS_FOCUS) {
    RECT f = d->rcItem;
    InflateRect(&f, -2, -2);
    DrawFocusRect(d->hDC, &f);
  }
}

LRESULT CALLBACK fields_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  Editor* e = g_editor;
  switch (msg) {
    case WM_VSCROLL: {
      if (!e)
        break;
      SCROLLINFO si = {sizeof(si), SIF_ALL};
      GetScrollInfo(hwnd, SB_VERT, &si);
      int pos = e->scroll;
      switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= epx(30); break;
        case SB_LINEDOWN: pos += epx(30); break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = e->content_height; break;
      }
      scroll_to(pos);
      return 0;
    }
    case WM_MOUSEWHEEL:
      if (e)
        scroll_to(e->scroll -
                  MulDiv(GET_WHEEL_DELTA_WPARAM(wp), epx(90), WHEEL_DELTA));
      return 0;
    case WM_SIZE:
      if (e) {
        update_scrollbar();
        scroll_to(e->scroll);
      }
      return 0;
    case WM_HSCROLL: {
      if (!e || e->syncing)
        break;
      HWND bar = (HWND)lp;
      for (auto& r : e->rows) {
        if (r.control != bar)
          continue;
        const double step = r.field.step > 0 ? r.field.step : 1;
        const LRESULT pos = SendMessageW(bar, TBM_GETPOS, 0, 0);
        set_value(r, format_number(r.field.min + pos * step));
        break;
      }
      return 0;
    }
    case WM_COMMAND: {
      if (!e || e->syncing)
        break;
      const int id = LOWORD(wp);
      FieldRow* r = row_of(id);
      if (!r)
        break;
      const bool is_pick = (id - IDC_FIELD) % 2 == 1;
      const int code = HIWORD(wp);
      if (is_pick) {
        if (code == BN_CLICKED)
          pick_font(*r);
      } else if (r->field.kind == NOVA_THEME_COLOR && code == BN_CLICKED) {
        pick_color(*r);
      } else if (r->field.kind == NOVA_THEME_CHOICE &&
                 code == CBN_SELCHANGE) {
        const int sel = ComboBox_GetCurSel(r->control);
        if (sel >= 0 && sel < (int)r->choices.size())
          set_value(*r, r->choices[sel]);
      } else if (r->field.kind == NOVA_THEME_TEXT && code == EN_CHANGE) {
        wchar_t buf[512];
        GetWindowTextW(r->control, buf, 512);
        // 打字时不回写输入框，免得光标跳到开头。
        e->syncing = true;
        nova_ui_theme_set(e->theme.get(), r->key.c_str(),
                          nova_theme::utf8(std::wstring(buf)).c_str());
        e->syncing = false;
        set_preview_theme(e->preview, e->theme);
        refresh_problems();
      }
      return 0;
    }
    case WM_DRAWITEM:
      draw_swatch((const DRAWITEMSTRUCT*)lp);
      return TRUE;
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      SetBkMode(dc, TRANSPARENT);
      if (e && e->notes.count((HWND)lp))
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
      return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void close_editor() {
  if (g_editor && g_editor->hwnd)
    DestroyWindow(g_editor->hwnd);
}

void save_editor() {
  Editor& e = *g_editor;
  const fs::path path = user_path(e.id);
  if (!save_theme(e.theme.get(), path)) {
    MessageBoxW(e.hwnd, path.c_str(), L"存不了主题文件", MB_OK | MB_ICONERROR);
    return;
  }
  // 跟随系统时，存到和主题深浅对应的那一边；否则直接选用它。
  if (nova_theme::selection() == nova_theme::kAuto) {
    if (is_dark(e.theme.get()))
      nova_theme::set_dark_id(e.id);
    else
      nova_theme::set_light_id(e.id);
  } else {
    nova_theme::set_selection(e.id);
  }
  if (e.notify)
    PostMessageW(e.notify, WM_NOVA_THEMES_CHANGED, 0, 0);
  close_editor();
}

void layout_editor() {
  Editor& e = *g_editor;
  RECT rc;
  GetClientRect(e.hwnd, &rc);
  const int left_w = epx(440);
  MoveWindow(e.fields, 0, 0, left_w, rc.bottom, TRUE);
  const int x = left_w + epx(16);
  const int w = rc.right - x - epx(16);
  int y = epx(12);
  // 标题「预览」是第一个子窗口之后建的静态文字，这里只排预览以下的东西。
  y += epx(28);
  MoveWindow(e.preview, x, y, w, epx(230), TRUE);
  y += epx(242);
  MoveWindow(e.problems, x, y, w, epx(96), TRUE);
  y += epx(104);
  MoveWindow(e.path, x, y, w, epx(40), TRUE);
  HWND save = GetDlgItem(e.hwnd, IDC_SAVE);
  HWND cancel = GetDlgItem(e.hwnd, IDC_CANCEL);
  const int by = rc.bottom - epx(44);
  MoveWindow(save, rc.right - epx(16 + 120), by, epx(120), epx(30), TRUE);
  MoveWindow(cancel, rc.right - epx(16 + 120 + 10 + 90), by, epx(90), epx(30),
             TRUE);
}

LRESULT CALLBACK editor_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      if (g_editor && g_editor->fields)
        layout_editor();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mm = (MINMAXINFO*)lp;
      if (g_editor) {
        mm->ptMinTrackSize.x = epx(820);
        mm->ptMinTrackSize.y = epx(560);
      }
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wp) == IDC_SAVE || LOWORD(wp) == IDOK) {
        save_editor();
        return 0;
      }
      if (LOWORD(wp) == IDC_CANCEL || LOWORD(wp) == IDCANCEL) {
        close_editor();
        return 0;
      }
      break;
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      SetBkMode(dc, TRANSPARENT);
      if (g_editor && (HWND)lp == g_editor->problems &&
          nova_ui_theme_check(g_editor->theme.get())[0])
        SetTextColor(dc, RGB(0xc2, 0x41, 0x0c));
      else if (g_editor && (HWND)lp == g_editor->path)
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
      return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CLOSE:
      close_editor();
      return 0;
    case WM_DESTROY:
      if (g_editor) {
        HWND owner = g_editor->notify;
        delete g_editor;
        g_editor = nullptr;
        if (owner) {
          EnableWindow(owner, TRUE);
          SetForegroundWindow(owner);
        }
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// 打开编辑器。theme 是一份拷贝，存之前不影响正在用的主题。
void open_editor(HWND owner,
                 const Fonts& fonts,
                 nova_theme::ThemePtr theme,
                 const std::wstring& id) {
  if (g_editor) {
    SetForegroundWindow(g_editor->hwnd);
    return;
  }
  g_editor = new Editor();
  Editor& e = *g_editor;
  e.fonts = fonts;
  e.theme = std::move(theme);
  e.id = id;
  e.notify = owner;
  for (auto& c : e.custom)
    c = RGB(255, 255, 255);

  RECT r = {0, 0, epx(900), epx(640)};
  const DWORD style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
  AdjustWindowRectEx(&r, style, FALSE, WS_EX_DLGMODALFRAME);
  RECT o;
  GetWindowRect(owner, &o);
  e.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kEditorClass, L"编辑主题",
                           style, o.left + epx(30), o.top + epx(30),
                           r.right - r.left, r.bottom - r.top, owner, NULL,
                           g_inst, NULL);
  if (!e.hwnd) {
    delete g_editor;
    g_editor = nullptr;
    return;
  }
  e.fields = CreateWindowExW(0, kFieldsClass, L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                 WS_CLIPCHILDREN | WS_TABSTOP,
                             0, 0, epx(440), epx(640), e.hwnd, NULL, g_inst,
                             NULL);
  SetWindowLongPtrW(e.fields, GWL_EXSTYLE,
                    GetWindowLongPtrW(e.fields, GWL_EXSTYLE) |
                        WS_EX_CONTROLPARENT);
  Maker m{e.hwnd, &e.fonts};
  m.make(L"STATIC", L"预览", SS_LEFT, 456, 12, 200, 22, 0, true);
  e.preview = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE, 0,
                              0, 10, 10, e.hwnd, NULL, g_inst, NULL);
  e.problems = m.make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 0, 10, 10);
  const std::wstring where =
      L"存在设置文件夹的 nova_themes\\" + e.id + L".yaml。";
  e.path = m.make(L"STATIC", where.c_str(), SS_LEFT | SS_NOPREFIX, 0, 0, 10,
                  10);
  m.make(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10,
         IDC_CANCEL);
  m.make(L"BUTTON", L"存储并使用", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 0, 10, 10,
         IDC_SAVE);

  build_fields();
  set_preview_theme(e.preview, e.theme);
  refresh_problems();
  layout_editor();
  EnableWindow(owner, FALSE);
  ShowWindow(e.hwnd, SW_SHOW);
  SetForegroundWindow(e.hwnd);
}

// ---- 外观页 ----

struct Page {
  HWND parent = NULL;
  Fonts fonts;
  HWND theme = NULL, light = NULL, dark = NULL;
  HWND light_label = NULL, dark_label = NULL;
  HWND preview = NULL;
  HWND remove = NULL;
  HWND message = NULL;
  std::vector<Entry> entries;
  std::vector<std::wstring> light_ids, dark_ids;  // 两个下拉框里的顺序
};

Page g_page;

std::wstring shown_id() {
  const std::wstring sel = nova_theme::selection();
  if (sel != nova_theme::kAuto)
    return sel;
  return nova_theme::system_dark() ? nova_theme::dark_id()
                                   : nova_theme::light_id();
}

const Entry* entry_of(const std::wstring& id) {
  for (const Entry& e : g_page.entries)
    if (e.id == id)
      return &e;
  return nullptr;
}

// 一侧（浅色或深色）的下拉：同深浅的在前，其余在后。
void fill_side(HWND combo,
               std::vector<std::wstring>* ids,
               bool dark,
               const std::wstring& current) {
  ComboBox_ResetContent(combo);
  ids->clear();
  for (int pass = 0; pass < 2; ++pass)
    for (const Entry& e : g_page.entries)
      if ((e.dark == dark) == (pass == 0)) {
        ComboBox_AddString(combo, e.name.c_str());
        ids->push_back(e.id);
      }
  int sel = -1;
  for (size_t i = 0; i < ids->size(); ++i)
    if ((*ids)[i] == current)
      sel = (int)i;
  ComboBox_SetCurSel(combo, sel);
}

void refresh_page() {
  Page& p = g_page;
  p.entries = list_themes();
  const std::wstring sel = nova_theme::selection();
  ComboBox_ResetContent(p.theme);
  ComboBox_AddString(p.theme, L"跟随系统");
  int index = 0;
  for (size_t i = 0; i < p.entries.size(); ++i) {
    ComboBox_AddString(p.theme, p.entries[i].name.c_str());
    if (p.entries[i].id == sel)
      index = (int)i + 1;
  }
  ComboBox_SetCurSel(p.theme, index);
  fill_side(p.light, &p.light_ids, false, nova_theme::light_id());
  fill_side(p.dark, &p.dark_ids, true, nova_theme::dark_id());
  const bool automatic = sel == nova_theme::kAuto;
  EnableWindow(p.light, automatic);
  EnableWindow(p.dark, automatic);
  EnableWindow(p.light_label, automatic);
  EnableWindow(p.dark_label, automatic);

  const std::wstring id = shown_id();
  const Entry* e = entry_of(id);
  EnableWindow(p.remove, e && e->user);
  SetWindowTextW(p.remove, e && e->overrides_builtin ? L"恢复自带" : L"删除");
  set_preview_theme(p.preview, nova_theme::load(id));
}

void message(const std::wstring& text) {
  SetWindowTextW(g_page.message, text.c_str());
}

void edit_current(bool duplicate) {
  const std::wstring id = shown_id();
  nova_theme::ThemePtr theme =
      nova_theme::wrap(nova_ui_theme_copy(nova_theme::load(id).get()));
  std::wstring target = id;
  if (duplicate) {
    const std::wstring name = get(theme.get(), "name");
    nova_ui_theme_set(theme.get(), "name",
                      nova_theme::utf8(name + L" 副本").c_str());
    nova_ui_theme_set(theme.get(), "author", "");
    target = free_id(L"custom");
  }
  open_editor(g_page.parent, g_page.fonts, theme, target);
}

void remove_current() {
  const Entry* e = entry_of(shown_id());
  if (!e || !e->user)
    return;
  const Entry entry = *e;
  const std::wstring title = entry.overrides_builtin
                                 ? L"把「" + entry.name + L"」恢复成自带的样子？"
                                 : L"删除主题「" + entry.name + L"」？";
  const std::wstring body = title + L"\r\n\r\n会删掉设置文件夹里的 nova_themes\\" +
                            entry.id + L".yaml。";
  if (MessageBoxW(g_page.parent, body.c_str(), L"Nova 星语",
                  MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
    return;
  std::error_code ec;
  fs::remove(nova_theme::user_dir() / (entry.id + L".yaml"), ec);
  if (nova_theme::selection() == entry.id && !entry.overrides_builtin)
    nova_theme::set_selection(nova_theme::kAuto);
  if (!entry.overrides_builtin) {
    if (nova_theme::light_id() == entry.id)
      nova_theme::set_light_id(L"light");
    if (nova_theme::dark_id() == entry.id)
      nova_theme::set_dark_id(L"dark");
  }
  message(L"");
  refresh_page();
}

void import_theme() {
  wchar_t file[MAX_PATH] = {0};
  OPENFILENAMEW of = {sizeof(of)};
  of.hwndOwner = g_page.parent;
  of.lpstrFilter = L"Nova 星语主题 (*.yaml;*.yml)\0*.yaml;*.yml\0所有文件\0*.*\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrTitle = L"选一个 Nova 星语的主题文件（.yaml）";
  of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&of))
    return;
  const fs::path src(file);
  nova_theme::ThemePtr theme(
      nova_ui_theme_load(nova_theme::utf8(src).c_str()),
      nova_theme::ThemeDeleter());
  if (!theme) {
    message(L"读不了 " + src.filename().wstring() + L"。");
    return;
  }
  const std::wstring id = free_id(src.stem().wstring());
  if (!save_theme(theme.get(), user_path(id))) {
    message(L"存不了主题文件：" + user_path(id).wstring());
    return;
  }
  nova_theme::set_selection(id);
  const size_t skipped = lines(nova_ui_theme_problems(theme.get())).size();
  std::wstring text = L"已导入「" + get(theme.get(), "name") + L"」并选用。";
  if (skipped)
    text += L"跳过了 " + std::to_wstring(skipped) + L" 行不认识的设置。";
  message(text);
  refresh_page();
}

void export_theme() {
  const std::wstring id = shown_id();
  wchar_t file[MAX_PATH] = {0};
  wcsncpy_s(file, (id + L".yaml").c_str(), MAX_PATH - 1);
  OPENFILENAMEW of = {sizeof(of)};
  of.hwndOwner = g_page.parent;
  of.lpstrFilter = L"Nova 星语主题 (*.yaml)\0*.yaml\0";
  of.lpstrFile = file;
  of.nMaxFile = MAX_PATH;
  of.lpstrDefExt = L"yaml";
  of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&of))
    return;
  nova_theme::ThemePtr theme = nova_theme::load(id);
  message(save_theme(theme.get(), fs::path(file))
              ? L"已导出到 " + std::wstring(file) + L"。"
              : std::wstring(L"导出失败。"));
}

// 设置窗口的子类过程：接编辑器存好以后的通知。
WNDPROC g_parent_proc = nullptr;

LRESULT CALLBACK parent_hook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NOVA_THEMES_CHANGED) {
    message(L"已存储并选用。");
    refresh_page();
    return 0;
  }
  if (msg == WM_SETTINGCHANGE && lp &&
      wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
    // 系统换了深浅色，跟随系统时预览跟着换。
    refresh_page();
  }
  return CallWindowProcW(g_parent_proc, hwnd, msg, wp, lp);
}

}  // namespace

void Register(HINSTANCE inst) {
  g_inst = inst;
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
  InitCommonControlsEx(&icc);

  WNDCLASSEXW wc = {sizeof(wc)};
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);

  wc.lpfnWndProc = preview_proc;
  wc.lpszClassName = kPreviewClass;
  RegisterClassExW(&wc);

  wc.lpfnWndProc = fields_proc;
  wc.lpszClassName = kFieldsClass;
  wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  RegisterClassExW(&wc);

  wc.lpfnWndProc = editor_proc;
  wc.lpszClassName = kEditorClass;
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  RegisterClassExW(&wc);
}

void BuildPage(HWND parent,
               const Fonts& fonts,
               int x,
               int y,
               int w,
               std::vector<HWND>* controls) {
  Page& p = g_page;
  p.parent = parent;
  p.fonts = fonts;
  Maker m{parent, &fonts};
  auto keep = [&](HWND h) {
    controls->push_back(h);
    return h;
  };

  keep(m.make(L"STATIC", L"主题", SS_LEFT, x, y + 4, 60, 20));
  p.theme = keep(m.make(WC_COMBOBOXW, L"",
                        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 64, y,
                        220, 300, IDC_THEME));
  y += 34;
  p.light_label = keep(m.make(L"STATIC", L"浅色时", SS_LEFT, x, y + 4, 60, 20));
  p.light = keep(m.make(WC_COMBOBOXW, L"",
                        CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 64, y,
                        150, 300, IDC_LIGHT));
  p.dark_label =
      keep(m.make(L"STATIC", L"深色时", SS_LEFT, x + 230, y + 4, 60, 20));
  p.dark = keep(m.make(WC_COMBOBOXW, L"",
                       CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + 294, y,
                       150, 300, IDC_DARK));
  y += 36;
  p.preview = keep(CreateWindowExW(
      0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE, m.px(x), m.px(y),
      m.px(w), m.px(196), parent, NULL, g_inst, NULL));
  y += 204;
  keep(m.make(L"BUTTON", L"编辑…", BS_PUSHBUTTON | WS_TABSTOP, x, y, 70, 28,
              IDC_EDIT));
  keep(m.make(L"BUTTON", L"复制成新主题…", BS_PUSHBUTTON | WS_TABSTOP, x + 76,
              y, 112, 28, IDC_DUPLICATE));
  p.remove = keep(m.make(L"BUTTON", L"删除", BS_PUSHBUTTON | WS_TABSTOP,
                         x + 194, y, 76, 28, IDC_REMOVE));
  keep(m.make(L"BUTTON", L"导入…", BS_PUSHBUTTON | WS_TABSTOP, x + w - 150, y,
              72, 28, IDC_IMPORT));
  keep(m.make(L"BUTTON", L"导出…", BS_PUSHBUTTON | WS_TABSTOP, x + w - 72, y,
              72, 28, IDC_EXPORT));
  y += 34;
  p.message = keep(m.make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, y, w, 20));
  y += 22;
  keep(m.make(L"STATIC",
              L"主题包括颜色、字体、字号和形状。主题文件在设置文件夹的 "
              L"nova_themes 里，也可以直接用编辑器改。",
              SS_LEFT, x, y, w, 36));

  if (!g_parent_proc)
    g_parent_proc = (WNDPROC)SetWindowLongPtrW(parent, GWLP_WNDPROC,
                                                (LONG_PTR)parent_hook);
  refresh_page();
}

bool OnCommand(HWND parent, WPARAM wp, LPARAM lp) {
  const int id = LOWORD(wp);
  const int code = HIWORD(wp);
  Page& p = g_page;
  switch (id) {
    case IDC_THEME:
      if (code == CBN_SELCHANGE) {
        const int sel = ComboBox_GetCurSel(p.theme);
        if (sel == 0)
          nova_theme::set_selection(nova_theme::kAuto);
        else if (sel > 0 && sel <= (int)p.entries.size())
          nova_theme::set_selection(p.entries[sel - 1].id);
        message(L"");
        refresh_page();
      }
      return true;
    case IDC_LIGHT:
    case IDC_DARK:
      if (code == CBN_SELCHANGE) {
        const bool light = id == IDC_LIGHT;
        const auto& ids = light ? p.light_ids : p.dark_ids;
        const int sel = ComboBox_GetCurSel(light ? p.light : p.dark);
        if (sel >= 0 && sel < (int)ids.size()) {
          if (light)
            nova_theme::set_light_id(ids[sel]);
          else
            nova_theme::set_dark_id(ids[sel]);
        }
        refresh_page();
      }
      return true;
  }
  if (code != BN_CLICKED)
    return false;
  switch (id) {
    case IDC_EDIT:
      edit_current(false);
      return true;
    case IDC_DUPLICATE:
      edit_current(true);
      return true;
    case IDC_REMOVE:
      remove_current();
      return true;
    case IDC_IMPORT:
      import_theme();
      return true;
    case IDC_EXPORT:
      export_theme();
      return true;
  }
  return false;
}

bool PreTranslate(MSG* msg) {
  if (!g_editor || !g_editor->hwnd)
    return false;
  // 滚轮在字段列表上方时滚列表，不让滑块或下拉框吃掉。
  if (msg->message == WM_MOUSEWHEEL) {
    POINT pt = {GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
    HWND under = WindowFromPoint(pt);
    if (under == g_editor->fields || IsChild(g_editor->fields, under)) {
      HWND focus = GetFocus();
      const bool dropped = focus && SendMessageW(focus, CB_GETDROPPEDSTATE,
                                                 0, 0);
      if (!dropped) {
        SendMessageW(g_editor->fields, WM_MOUSEWHEEL, msg->wParam,
                     msg->lParam);
        return true;
      }
    }
  }
  return IsDialogMessageW(g_editor->hwnd, msg) != FALSE;
}

}  // namespace nova_theme_ui
