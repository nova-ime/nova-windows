#pragma once
// 星语的设置窗口：WeaselDeployer.exe /nova，托盘菜单「设置…」打开它。
// 对应 Mac 版 sources/NovaSettings.swift，四页：输入方式、选项（含辅助码）、使用说明、高级。
//
// 输入方式和两个开关写进注册表（NovaOptions.h），再让 WeaselServer 下一次按键时设给所有会话。
// 换辅助码在后台线程里跑 nova_aux::Builder，窗口每 100 毫秒读一次进度；换好后进度区收起。

// 只有一个窗口，同时只开一个；已经开着时再打开就把它提到前面。
int ShowNovaSettings();
bool ActivateNovaSettings();
