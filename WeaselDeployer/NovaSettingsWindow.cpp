#include "stdafx.h"
#include "NovaSettingsWindow.h"
#include "NovaAux.h"
#include <WeaselIPC.h>
#include <WeaselUtility.h>
#include <NovaOptions.h>
#include <atlbase.h>
#include <atlhost.h>
#include <commctrl.h>
#include <windowsx.h>
#include "resource.h"
#include <shellapi.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kClass[] = L"NovaSettingsWindow";
constexpr UINT_PTR kTimer = 1;

enum : int {
  IDC_TAB = 100,
  IDC_LAYOUT = 110,   // 110…114，顺序同 nova::kLayouts
  IDC_TOGGLE = 120,   // 120…121，顺序同 nova::kToggles
  IDC_AUX = 130,      // 130…132，顺序同 nova_aux::kSchemes
  IDC_OPEN_DIR = 140,
  IDC_RELOAD = 141,
  IDC_OPEN_HELP = 142,
};

enum Page { kLayoutPage, kOptionsPage, kHelpPage, kAdvancedPage, kPageCount };

struct Window {
  HWND hwnd = NULL;
  HWND tab = NULL;
  HFONT font = NULL, bold = NULL;
  int dpi = 96;
  std::vector<HWND> pages[kPageCount];

  HWND layouts[std::size(nova::kLayouts)] = {};
  HWND toggles[std::size(nova::kToggles)] = {};
  HWND aux[nova_aux::kSchemeCount] = {};
  HWND progress = NULL, status = NULL;
  HWND stages[nova_aux::kStageCount] = {};
  HWND help = NULL;
  HWND reload = NULL, reload_status = NULL;

  nova_aux::Builder builder;
  std::thread worker;
  bool reloading = false;  // 正在跑的是「重新加载设置」而不是换辅助码
  bool showing_progress = false;

  int px(int dip) const { return MulDiv(dip, dpi, 96); }
};

Window* g = nullptr;

void notify_server() {
  weasel::Client client;
  if (client.Connect())
    client.TrayCommand(ID_NOVA_OPTIONS_CHANGED);
}

HWND add(Page page,
         const wchar_t* cls,
         const wchar_t* text,
         DWORD style,
         int x,
         int y,
         int w,
         int h,
         int id = 0,
         bool bold = false) {
  HWND c = CreateWindowExW(0, cls, text, WS_CHILD | style, g->px(x), g->px(y),
                           g->px(w), g->px(h), g->hwnd,
                           (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
  SendMessageW(c, WM_SETFONT, (WPARAM)(bold ? g->bold : g->font), TRUE);
  g->pages[page].push_back(c);
  return c;
}

HWND label(Page page, const wchar_t* text, int x, int y, int w, int h = 20,
           bool bold = false) {
  return add(page, L"STATIC", text, SS_LEFT, x, y, w, h, 0, bold);
}

void show_page(int page) {
  for (int p = 0; p < kPageCount; ++p)
    for (HWND c : g->pages[p])
      ShowWindow(c, p == page ? SW_SHOW : SW_HIDE);
  if (page == kOptionsPage && !g->showing_progress) {
    ShowWindow(g->progress, SW_HIDE);
    ShowWindow(g->status, SW_HIDE);
    for (HWND s : g->stages)
      ShowWindow(s, SW_HIDE);
  }
}

void sync_from_registry() {
  const std::wstring cur = nova::layout();
  for (size_t i = 0; i < std::size(nova::kLayouts); ++i)
    Button_SetCheck(g->layouts[i],
                    cur == nova::kLayouts[i].key ? BST_CHECKED : BST_UNCHECKED);
  for (size_t i = 0; i < std::size(nova::kToggles); ++i)
    Button_SetCheck(g->toggles[i], nova::toggle(nova::kToggles[i])
                                       ? BST_CHECKED
                                       : BST_UNCHECKED);
  const std::wstring aux = nova_aux::current();
  for (int i = 0; i < nova_aux::kSchemeCount; ++i)
    Button_SetCheck(g->aux[i], aux == nova_aux::kSchemes[i].id ? BST_CHECKED
                                                                : BST_UNCHECKED);
}

void set_busy(bool busy) {
  for (HWND b : g->aux)
    EnableWindow(b, !busy);
  EnableWindow(g->reload, !busy);
}

std::wstring seconds(double s) {
  wchar_t buf[32];
  swprintf_s(buf, L"%.0f 秒", s);
  return buf;
}

// 每 100 毫秒读一次 builder 的进度。
void tick() {
  auto& b = g->builder;
  const bool running = b.running();
  if (g->reloading) {
    if (!running) {
      KillTimer(g->hwnd, kTimer);
      if (g->worker.joinable())
        g->worker.join();
      g->reloading = false;
      set_busy(false);
      const std::wstring err = b.error();
      SetWindowTextW(g->reload_status,
                     err.empty() ? L"已重新加载。" : err.c_str());
    }
    return;
  }

  const int stage = b.stage();
  SendMessageW(g->progress, PBM_SETPOS, (WPARAM)(b.fraction() * 1000), 0);
  for (int i = 0; i < nova_aux::kStageCount; ++i) {
    std::wstring text = i < stage ? L"✓  " : (i == stage ? L"▸  " : L"    ");
    text += nova_aux::kStages[i];
    SetWindowTextW(g->stages[i], text.c_str());
  }
  if (running) {
    std::wstring s = L"正在换辅助码，已用 " + seconds(b.elapsed()) +
                     L"。期间照常打字。";
    SetWindowTextW(g->status, s.c_str());
    return;
  }

  KillTimer(g->hwnd, kTimer);
  if (g->worker.joinable())
    g->worker.join();
  set_busy(false);
  sync_from_registry();
  const std::wstring err = b.error();
  if (err.empty()) {
    // 换好了：进度区收起，只在下面留一行结果。
    g->showing_progress = false;
    ShowWindow(g->progress, SW_HIDE);
    for (HWND s : g->stages)
      ShowWindow(s, SW_HIDE);
    std::wstring s = L"已换成" + nova_aux::scheme_name(nova_aux::current()) +
                     L"，用时 " + seconds(b.elapsed()) + L"。";
    SetWindowTextW(g->status, s.c_str());
  } else {
    SetWindowTextW(g->status, (L"没换成：" + err).c_str());
  }
}

void start_aux(int index) {
  const std::wstring scheme = nova_aux::kSchemes[index].id;
  if (scheme == nova_aux::current() && !nova_aux::needs_rebuild())
    return;
  g->showing_progress = true;
  g->reloading = false;
  set_busy(true);
  SendMessageW(g->progress, PBM_SETPOS, 0, 0);
  ShowWindow(g->progress, SW_SHOW);
  ShowWindow(g->status, SW_SHOW);
  for (HWND s : g->stages)
    ShowWindow(s, SW_SHOW);
  g->worker = std::thread([scheme] { g->builder.run(scheme); });
  SetTimer(g->hwnd, kTimer, 100, NULL);
  tick();
}

void start_reload() {
  g->reloading = true;
  set_busy(true);
  SetWindowTextW(g->reload_status, L"正在重新加载…");
  g->worker = std::thread([] { g->builder.reload(); });
  SetTimer(g->hwnd, kTimer, 100, NULL);
}

std::filesystem::path help_path() {
  return WeaselSharedDataPath() / L"help.html";
}

void build() {
  RECT rc;
  GetClientRect(g->hwnd, &rc);
  g->tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, g->px(8),
                           g->px(8), rc.right - g->px(16),
                           rc.bottom - g->px(16), g->hwnd,
                           (HMENU)(INT_PTR)IDC_TAB, GetModuleHandle(NULL), NULL);
  SendMessageW(g->tab, WM_SETFONT, (WPARAM)g->font, TRUE);
  const wchar_t* titles[kPageCount] = {L"输入方式", L"选项", L"使用说明",
                                       L"高级"};
  for (int i = 0; i < kPageCount; ++i) {
    TCITEMW item = {TCIF_TEXT};
    item.pszText = (LPWSTR)titles[i];
    TabCtrl_InsertItem(g->tab, i, &item);
  }

  const int x = 28, top = 48, w = 460;

  // 输入方式
  label(kLayoutPage, L"选你习惯的打法，马上生效。", x, top, w);
  const wchar_t* layout_names[] = {L"自然码双拼", L"小鹤双拼", L"微软双拼",
                                   L"搜狗双拼", L"全拼"};
  for (size_t i = 0; i < std::size(nova::kLayouts); ++i)
    g->layouts[i] =
        add(kLayoutPage, L"BUTTON", layout_names[i],
            BS_AUTORADIOBUTTON | WS_TABSTOP | (i == 0 ? WS_GROUP : 0), x + 8,
            top + 32 + (int)i * 28, 240, 24, IDC_LAYOUT + (int)i);
  label(kLayoutPage,
        L"托盘菜单「输入方式」里也能换。全拼可以只打声母，也会自动纠正常见的打错键。",
        x, top + 32 + 5 * 28 + 12, w, 40);

  // 选项
  const wchar_t* toggle_names[] = {L"全角字符", L"中文状态下用英文标点"};
  for (size_t i = 0; i < std::size(nova::kToggles); ++i)
    g->toggles[i] =
        add(kOptionsPage, L"BUTTON", toggle_names[i],
            BS_AUTOCHECKBOX | WS_TABSTOP, x, top + (int)i * 28, 300, 24,
            IDC_TOGGLE + (int)i);
  int y = top + 2 * 28 + 16;
  label(kOptionsPage, L"辅助码", x, y, w, 20, true);
  label(kOptionsPage,
        L"打完拼音后，按住 Shift 打两个大写字母，按字形筛字。"
        L"换辅助码要在本机重新编译词库，约一分钟，期间照常打字。",
        x, y + 24, w, 40);
  y += 70;
  for (int i = 0; i < nova_aux::kSchemeCount; ++i)
    g->aux[i] = add(kOptionsPage, L"BUTTON", nova_aux::kSchemes[i].name,
                    BS_AUTORADIOBUTTON | WS_TABSTOP | (i == 0 ? WS_GROUP : 0),
                    x + 8 + i * 140, y, 130, 24, IDC_AUX + i);
  y += 36;
  g->progress = add(kOptionsPage, PROGRESS_CLASSW, L"", PBS_SMOOTH, x, y, w, 14);
  SendMessageW(g->progress, PBM_SETRANGE32, 0, 1000);
  y += 22;
  for (int i = 0; i < nova_aux::kStageCount; ++i)
    g->stages[i] = label(kOptionsPage, nova_aux::kStages[i], x + 8 + (i % 3) * 150,
                         y + (i / 3) * 22, 145);
  y += 50;
  g->status = label(kOptionsPage, L"", x, y, w, 40);

  // 使用说明：用系统自带的网页控件显示安装目录里的 help.html。
  const std::wstring url = L"file:///" + help_path().wstring();
  g->help = add(kHelpPage, L"AtlAxWin", url.c_str(), WS_VSCROLL, 16, 40,
                MulDiv(rc.right, 96, g->dpi) - 32,
                MulDiv(rc.bottom, 96, g->dpi) - 90);
  add(kHelpPage, L"BUTTON", L"在浏览器里打开", BS_PUSHBUTTON | WS_TABSTOP,
      16, MulDiv(rc.bottom, 96, g->dpi) - 44, 140, 28, IDC_OPEN_HELP);

  // 高级
  label(kAdvancedPage,
        L"一般设置都在前两页。想细调的话，可以用编辑器打开设置文件夹里的 "
        L"nova.custom.yaml，改完点「重新加载设置」。",
        x, top, w, 40);
  add(kAdvancedPage, L"BUTTON", L"打开设置文件夹", BS_PUSHBUTTON | WS_TABSTOP,
      x, top + 52, 160, 30, IDC_OPEN_DIR);
  g->reload = add(kAdvancedPage, L"BUTTON", L"重新加载设置",
                  BS_PUSHBUTTON | WS_TABSTOP, x + 172, top + 52, 160, 30,
                  IDC_RELOAD);
  g->reload_status = label(kAdvancedPage, L"", x, top + 96, w, 40);

  sync_from_registry();
  show_page(kLayoutPage);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_NOTIFY: {
      const NMHDR* n = (const NMHDR*)lp;
      if (n->idFrom == IDC_TAB && n->code == TCN_SELCHANGE)
        show_page(TabCtrl_GetCurSel(g->tab));
      break;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp);
      if (HIWORD(wp) != BN_CLICKED)
        break;
      if (id >= IDC_LAYOUT && id < IDC_LAYOUT + (int)std::size(nova::kLayouts)) {
        nova::set_layout(nova::kLayouts[id - IDC_LAYOUT].key);
        notify_server();
      } else if (id >= IDC_TOGGLE &&
                 id < IDC_TOGGLE + (int)std::size(nova::kToggles)) {
        const auto& t = nova::kToggles[id - IDC_TOGGLE];
        nova::set_toggle(t, Button_GetCheck((HWND)lp) == BST_CHECKED);
        notify_server();
      } else if (id >= IDC_AUX && id < IDC_AUX + nova_aux::kSchemeCount) {
        if (!g->builder.running())
          start_aux(id - IDC_AUX);
      } else if (id == IDC_OPEN_DIR) {
        ShellExecuteW(NULL, L"open", WeaselUserDataPath().c_str(), NULL, NULL,
                      SW_SHOWNORMAL);
      } else if (id == IDC_RELOAD) {
        if (!g->builder.running())
          start_reload();
      } else if (id == IDC_OPEN_HELP) {
        ShellExecuteW(NULL, L"open", help_path().c_str(), NULL, NULL,
                      SW_SHOWNORMAL);
      }
      return 0;
    }
    case WM_TIMER:
      if (wp == kTimer)
        tick();
      return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      // 标签放在 Tab 控件上面，用 Tab 页的白底。
      HDC dc = (HDC)wp;
      SetBkMode(dc, TRANSPARENT);
      return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_CLOSE:
      if (g->builder.running()) {
        MessageBoxW(hwnd, L"词库还在编译，编好以后再关闭窗口。", L"Nova 星语",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
      }
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool ActivateNovaSettings() {
  HWND w = FindWindowW(kClass, NULL);
  if (!w)
    return false;
  if (IsIconic(w))
    ShowWindow(w, SW_RESTORE);
  SetForegroundWindow(w);
  return true;
}

int ShowNovaSettings() {
  AtlAxWinInit();
  INITCOMMONCONTROLSEX icc = {sizeof(icc),
                              ICC_TAB_CLASSES | ICC_PROGRESS_CLASS |
                                  ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  HINSTANCE inst = GetModuleHandle(NULL);
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
  wc.lpszClassName = kClass;
  wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_DEPLOY));
  RegisterClassExW(&wc);

  auto window = std::make_unique<Window>();
  g = window.get();
  {
    HDC sdc = GetDC(NULL);
    g->dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(NULL, sdc);
  }

  LOGFONTW lf = {};
  lf.lfHeight = -MulDiv(9, g->dpi, 72);
  lf.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
  g->font = CreateFontIndirectW(&lf);
  lf.lfWeight = FW_BOLD;
  g->bold = CreateFontIndirectW(&lf);

  RECT r = {0, 0, g->px(540), g->px(460)};
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRectEx(&r, style, FALSE, 0);
  g->hwnd = CreateWindowExW(0, kClass, L"Nova 星语 设置", style, CW_USEDEFAULT,
                            CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                            NULL, NULL, inst, NULL);
  if (!g->hwnd)
    return 1;
  build();
  ShowWindow(g->hwnd, SW_SHOW);
  SetForegroundWindow(g->hwnd);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    if (IsDialogMessageW(g->hwnd, &msg))
      continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (g->worker.joinable())
    g->worker.join();
  DeleteObject(g->font);
  DeleteObject(g->bold);
  g = nullptr;
  return 0;
}
