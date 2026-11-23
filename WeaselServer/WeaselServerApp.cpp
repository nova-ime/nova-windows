#include "stdafx.h"
#include "WeaselServerApp.h"
#include <filesystem>
#include <NovaOptions.h>

WeaselServerApp::WeaselServerApp()
    : m_handler(std::make_unique<RimeWithWeaselHandler>(&m_ui)),
      tray_icon(m_ui) {
  // m_handler.reset(new RimeWithWeaselHandler(&m_ui));
  LOG(INFO) << "WeaselServerApp constructed";
  m_server.SetRequestHandler(m_handler.get());
  SetupMenuHandlers();
}

WeaselServerApp::~WeaselServerApp() {}

int WeaselServerApp::Run() {
  LOG(INFO) << "starting IPC server window";
  if (!m_server.Start()) {
    LOG(ERROR) << "failed to start IPC server window";
    return -1;
  }
  LOG(INFO) << "IPC server window started";

  // Nova: no auto-update. WinSparkle pointed at the upstream Weasel feed,
  // which would offer to replace Nova with Weasel and made the IME go online.
  LOG(INFO) << "creating candidate UI";
  if (!m_ui.Create(m_server.GetHWnd())) {
    LOG(ERROR) << "failed to create candidate UI";
    return -1;
  }
  LOG(INFO) << "candidate UI created";

  LOG(INFO) << "initializing Rime handler";
  m_handler->Initialize();
  LOG(INFO) << "Rime handler initialized";
  m_handler->OnUpdateUI([this]() { tray_icon.RequestRefresh(); });

  LOG(INFO) << "creating tray icon";
  if (!tray_icon.Create(m_server.GetHWnd()))
    LOG(ERROR) << "failed to create tray icon";
  else
    LOG(INFO) << "tray icon created";
  m_server.SetTrayRefreshCallback([this]() { tray_icon.ApplyRefresh(); });
  tray_icon.RequestRefresh();

  LOG(INFO) << "entering IPC message loop";
  int ret = m_server.Run();
  LOG(INFO) << "IPC message loop returned: " << ret;

  tray_icon.DisableRefresh();
  m_handler->Finalize();
  m_ui.Destroy();
  tray_icon.RemoveIcon();

  return ret;
}

void WeaselServerApp::SetupMenuHandlers() {
  std::filesystem::path dir = install_dir();
  m_server.AddMenuHandler(ID_WEASELTRAY_QUIT,
                          [this] { return m_server.Stop() == 0; });
  m_server.AddMenuHandler(ID_WEASELTRAY_DEPLOY,
                          std::bind(execute, dir / L"WeaselDeployer.exe",
                                    std::wstring(L"/deploy")));
  // Nova：「设置…」打开星语的设置窗口（WeaselDeployer/NovaSettingsWindow.cpp）。
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SETTINGS,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/nova")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_DICT_MANAGEMENT,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/dict")));
  m_server.AddMenuHandler(
      ID_WEASELTRAY_SYNC,
      std::bind(execute, dir / L"WeaselDeployer.exe", std::wstring(L"/sync")));
  m_server.AddMenuHandler(ID_WEASELTRAY_WIKI,
                          std::bind(open, L"https://rime.im/docs/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_HOMEPAGE,
                          std::bind(open, L"https://rime.im/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_FORUM,
                          std::bind(open, L"https://rime.im/discuss/"));
  m_server.AddMenuHandler(ID_WEASELTRAY_CHECKUPDATE, check_update);
  m_server.AddMenuHandler(ID_WEASELTRAY_INSTALLDIR, std::bind(explore, dir));
  m_server.AddMenuHandler(ID_WEASELTRAY_USERCONFIG,
                          std::bind(explore, WeaselUserDataPath()));
  m_server.AddMenuHandler(ID_WEASELTRAY_LOGDIR,
                          std::bind(explore, WeaselLogPath()));
  // Nova：输入方式、两个开关存进注册表，下一次按键时设给所有会话。
  for (const auto& l : nova::kLayouts)
    m_server.AddMenuHandler(l.menu_id, [this, id = l.menu_id] {
      nova::handle_menu(id);
      m_handler->NovaOptionsChanged();
      return true;
    });
  for (const auto& t : nova::kToggles)
    m_server.AddMenuHandler(t.menu_id, [this, id = t.menu_id] {
      nova::handle_menu(id);
      m_handler->NovaOptionsChanged();
      return true;
    });
  // 设置窗口改了注册表后发这个命令，让所有会话在下一次按键时重新读。
  m_server.AddMenuHandler(ID_NOVA_OPTIONS_CHANGED, [this] {
    m_handler->NovaOptionsChanged();
    return true;
  });
  m_server.AddMenuHandler(ID_NOVA_HELP,
                          std::bind(open, (dir / L"data" / L"help.html").wstring()));
}
