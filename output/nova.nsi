; Nova 的 Windows 安装器。由 nova/package.sh 调用（makensis，在 Mac 上也能跑）。
; 和小狼毫原来的 install.nsi 的区别：
; - 只装 x64（Windows 10 及以上），装到 Program Files\Nova\nova-<版本>。
; - 只带 Nova 方案。方案、Lua、GPT-2 模型和预编译好的词库都在 nova-data\，装进 data\。
; - 多一页「选择输入方式」，写进 HKCU\Software\Rime\Weasel\NovaLayout（见 include/NovaOptions.h）。
; - 不弹小狼毫的方案设置窗口，不检查更新。
!include FileFunc.nsh
!include LogicLib.nsh
!include MUI2.nsh
!include nsDialogs.nsh
!include x64.nsh
!include WinVer.nsh

Unicode true

!ifndef NOVA_VERSION
!define NOVA_VERSION 1.0.2
!endif

!define NOVA_ROOT "$INSTDIR\nova-${NOVA_VERSION}"
!define REG_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Nova"
; 小狼毫的程序仍按这个键找安装位置、用户目录，不改。
!define WEASEL_KEY "Software\Rime\Weasel"

Name "Nova 星语 ${NOVA_VERSION}"
OutFile "archives\Nova-${NOVA_VERSION}-installer.exe"
InstallDir "$PROGRAMFILES64\Nova"

VIProductVersion "${NOVA_VERSION}.0"
VIAddVersionKey /LANG=2052 "CompanyName" "nova-ime"
VIAddVersionKey /LANG=2052 "ProductName" "Nova 星语"
VIAddVersionKey /LANG=2052 "FileDescription" "Nova 星语输入法安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${NOVA_VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "© 2026 nova-ime · GPL-3.0 · 源自 RIME 小狼毫（式恕堂）"

!define MUI_ICON ..\resource\weasel.ico
!define MUI_ABORTWARNING
SetCompressor /SOLID lzma
RequestExecutionLevel admin

Var Layout
Var RadioZrm
Var RadioFlypy
Var RadioMspy
Var RadioSogou
Var RadioQuanpin
Var OldRoot

!define MUI_WELCOMEPAGE_TITLE "安装 Nova 星语 ${NOVA_VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Nova 星语是整句输入法，用一个小型语言模型从整句上下文里挑字。$\r$\n$\r$\n安装需要约 300 MB 空间。如果装过小狼毫，安装程序会先把它移除，你的用户词库和设置文件保留不动。"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
Page custom LayoutPage LayoutLeave
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "Nova 星语装好了"
!define MUI_FINISHPAGE_TEXT "在任务栏的输入法列表里选「Nova 星语」就能用。$\r$\n$\r$\n托盘里的星语图标上点右键，可以换输入方式、打开使用说明。"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\data\help.html"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "打开使用说明"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

;--------------------------------

Function .onInit
  ${IfNot} ${AtLeastWin10}
  ${OrIfNot} ${IsNativeAMD64}
    MessageBox MB_OK|MB_ICONSTOP "Nova 星语需要 64 位（x64）的 Windows 10 或更新的系统。"
    Abort
  ${EndIf}
  ; 上次选的输入方式；没有就是自然码。静默安装（/S）直接沿用。
  ReadRegStr $Layout HKCU "${WEASEL_KEY}" "NovaLayout"
  ${If} $Layout == ""
    StrCpy $Layout "zrm"
  ${EndIf}
FunctionEnd

Function LayoutPage
  !insertmacro MUI_HEADER_TEXT "选择输入方式" "以后随时可以在托盘图标的右键菜单 →「输入方式」里改。"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 20u "你平时怎么打拼音？双拼和全拼学到的词互通，换了也不会丢。"
  Pop $0
  ${NSD_CreateRadioButton} 10u 24u 100% 14u "自然码双拼（Nova 的原生方案）"
  Pop $RadioZrm
  ${NSD_CreateRadioButton} 10u 40u 100% 14u "小鹤双拼"
  Pop $RadioFlypy
  ${NSD_CreateRadioButton} 10u 56u 100% 14u "微软双拼（ing 是 ;）"
  Pop $RadioMspy
  ${NSD_CreateRadioButton} 10u 72u 100% 14u "搜狗双拼（ing 是 ;）"
  Pop $RadioSogou
  ${NSD_CreateRadioButton} 10u 88u 100% 14u "全拼（可以只打声母：wm → 我们）"
  Pop $RadioQuanpin
  ${Select} $Layout
    ${Case} "flypy"
      ${NSD_Check} $RadioFlypy
    ${Case} "mspy"
      ${NSD_Check} $RadioMspy
    ${Case} "sogou"
      ${NSD_Check} $RadioSogou
    ${Case} "quanpin"
      ${NSD_Check} $RadioQuanpin
    ${CaseElse}
      ${NSD_Check} $RadioZrm
  ${EndSelect}
  nsDialogs::Show
FunctionEnd

Function LayoutLeave
  StrCpy $Layout "zrm"
  ${NSD_GetState} $RadioFlypy $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $Layout "flypy"
  ${EndIf}
  ${NSD_GetState} $RadioMspy $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $Layout "mspy"
  ${EndIf}
  ${NSD_GetState} $RadioSogou $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $Layout "sogou"
  ${EndIf}
  ${NSD_GetState} $RadioQuanpin $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $Layout "quanpin"
  ${EndIf}
FunctionEnd

; 停掉旧的服务，删掉旧程序（小狼毫或旧版 Nova）。用户目录（%APPDATA%\Rime）不动。
; 输入法的 TSF 注册保留，免得 Windows 趁空档塞一个默认输入法进来；新的 WeaselSetup 会重新注册。
Function RemoveOld
  ReadRegStr $OldRoot HKLM "${WEASEL_KEY}" "WeaselRoot"
  ${If} $OldRoot != ""
  ${AndIf} ${FileExists} "$OldRoot\WeaselServer.exe"
    DetailPrint "移除旧版本：$OldRoot"
    ExecWait '"$OldRoot\WeaselServer.exe" /quit'
    Sleep 800
    ; 同一版本重装就原地覆盖：正在被程序加载的 weasel*.dll 删不掉。
    StrCpy $0 "$INSTDIR\nova-${NOVA_VERSION}"
    ${If} $OldRoot != $0
      RMDir /r /REBOOTOK "$OldRoot"
    ${EndIf}
  ${EndIf}
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Weasel"
  SetShellVarContext all
  RMDir /r "$SMPROGRAMS\小狼毫输入法"
  RMDir /r "$SMPROGRAMS\小狼毫輸入法"
  SetRegView 64
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer"
  SetRegView 32
FunctionEnd

Section "Nova"
  SectionIn RO
  Call RemoveOld

  WriteRegStr HKLM "${WEASEL_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${WEASEL_KEY}" "NovaLayout" "$Layout"
  WriteRegStr HKCU "${WEASEL_KEY}\Updates" "CheckForUpdates" "0"

  StrCpy $INSTDIR "${NOVA_ROOT}"
  SetOverwrite on
  SetOutPath $INSTDIR
  File "LICENSE.txt"
  File "start_service.bat"
  File "stop_service.bat"
  File "weasel.dll"
  File "weaselx64.dll"
  File "WeaselDeployer.exe"
  File "WeaselServer.exe"
  File "WeaselSetup.exe"
  File "WeaselLanguageProfile.ps1"
  File "rime.dll"
  Delete "$INSTDIR\WinSparkle.dll"  ; 1.0.2 起不再自动更新

  SetOutPath $INSTDIR\data
  File /r "nova-data\*.*"

  SetOutPath $INSTDIR
  ; /s：注册成简体中文输入法，不弹选项窗口。
  ExecWait '"$INSTDIR\WeaselSetup.exe" /s'

  WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayName" "Nova 星语输入法"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayIcon" '"$INSTDIR\WeaselServer.exe"'
  WriteRegStr HKLM "${REG_UNINST_KEY}" "DisplayVersion" "${NOVA_VERSION}"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${REG_UNINST_KEY}" "Publisher" "nova-ime"
  WriteRegStr HKLM "${REG_UNINST_KEY}" "URLInfoAbout" "https://github.com/nova-ime/nova-windows"
  WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${REG_UNINST_KEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\uninstall.exe"

  DetailPrint "准备方案（第一次约半分钟）……"
  ExecWait '"$INSTDIR\WeaselDeployer.exe" /deploy'

  SetRegView 64
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer" "$INSTDIR\WeaselServer.exe"
  SetRegView 32
  Exec "$INSTDIR\WeaselServer.exe"

  SetShellVarContext all
  CreateDirectory "$SMPROGRAMS\Nova 星语"
  CreateShortCut "$SMPROGRAMS\Nova 星语\使用说明.lnk" "$INSTDIR\data\help.html"
  CreateShortCut "$SMPROGRAMS\Nova 星语\设置文件夹.lnk" "$INSTDIR\WeaselServer.exe" "/userdir" "$SYSDIR\shell32.dll" 126
  CreateShortCut "$SMPROGRAMS\Nova 星语\重新加载设置.lnk" "$INSTDIR\WeaselDeployer.exe" "/deploy" "$SYSDIR\shell32.dll" 144
  CreateShortCut "$SMPROGRAMS\Nova 星语\卸载 Nova 星语.lnk" "$INSTDIR\uninstall.exe" "" "$INSTDIR\uninstall.exe" 0
SectionEnd

;--------------------------------

Section "Uninstall"
  ExecWait '"$INSTDIR\WeaselServer.exe" /quit'
  ExecWait '"$INSTDIR\WeaselSetup.exe" /u'

  DeleteRegKey HKLM "Software\Rime"
  DeleteRegKey HKLM "${REG_UNINST_KEY}"
  SetRegView 64
  DeleteRegValue HKLM "Software\Microsoft\Windows\CurrentVersion\Run" "WeaselServer"
  SetRegView 32

  SetOutPath $TEMP
  RMDir /r /REBOOTOK "$INSTDIR"
  ${GetParent} "$INSTDIR" $0
  RMDir "$0"
  SetShellVarContext all
  RMDir /r "$SMPROGRAMS\Nova 星语"
  SetRebootFlag true
SectionEnd
