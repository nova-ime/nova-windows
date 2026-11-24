#pragma once
// 设置窗口的「外观」页和主题编辑器。对应 Mac 版 sources/NovaThemeEditor.swift。
// 字段的名字、分组、范围都来自 nova-ui（nova_ui_theme_fields），两边能改的东西一样；
// 预览用的是真正的面板绘制代码（WeaselUI 的 NovaRenderer）。
#include <windows.h>

#include <vector>

namespace nova_theme_ui {

struct Fonts {
  HFONT normal = NULL;
  HFONT bold = NULL;
  int dpi = 96;
};

// 注册预览控件和编辑器的窗口类。ShowNovaSettings 开头调一次。
void Register(HINSTANCE inst);

// 在 parent 上建「外观」页的控件，坐标是 96 dpi 的逻辑单位。建出的控件放进 controls，
// 由设置窗口按页显示和隐藏。
void BuildPage(HWND parent,
               const Fonts& fonts,
               int x,
               int y,
               int w,
               std::vector<HWND>* controls);

// 「外观」页控件的 WM_COMMAND。处理了返回 true。
bool OnCommand(HWND parent, WPARAM wp, LPARAM lp);

// 消息循环里先调它：编辑器的 Tab 键切换、滚轮滚动字段列表。处理了返回 true。
bool PreTranslate(MSG* msg);

}  // namespace nova_theme_ui
