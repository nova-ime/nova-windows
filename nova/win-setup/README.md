# Windows 编译环境

在一台全新的 Windows（x64）上从零搭出 Nova 的编译环境。不依赖任何一台旧机器上残留的文件：换机器时照这里做一遍就能编。

所有外部依赖的版本、提交号和下载校验值都写死在 `versions.bat`。

## 1. 手动装两样

都用默认安装路径。在**管理员命令提示符**里可以静默安装：

```bat
Git-2.56.1-64-bit.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES
vs_BuildTools.exe --quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.VC.ATL --add Microsoft.VisualStudio.Component.Windows11SDK.22621
```

- Git for Windows：https://github.com/git-for-windows/git/releases/download/v2.56.1.windows.1/Git-2.56.1-64-bit.exe
- VS 2022 Build Tools：https://aka.ms/vs/17/release/vs_BuildTools.exe 。要 MSVC v143、ATL、Windows 11 SDK 10.0.22621。不用装 MFC（`inc/afxres.h` 是替身）。

那台机器下载慢、或者上不了 GitHub 时，可以借 Mac 的网络：Mac 上跑一个 HTTP 代理，再用 `ssh -N -R 7898:127.0.0.1:7898 <用户>@<机器>` 把它转过去，Windows 上就能用 `http://127.0.0.1:7898`。

## 2. 跑 setup.bat

```bat
git clone -b nova https://github.com/nova-ime/nova-windows.git C:\nova\nova-windows
set NOVA_PROXY=http://127.0.0.1:7898
C:\nova\nova-windows\nova\win-setup\setup.bat
```

`NOVA_PROXY` 不需要代理就不设。可以重复运行，做完的步骤会跳过。最后一行是 `[setup] SETUP_OK` 就成了。

它会做这些事，全部装在 `C:\nova` 下（`NOVA_ROOT` 可改）：

- 下载 7zr、boost 1.84.0、librime 1.16.0 的预编译依赖，校验 sha256。
- 编 boost 的 x64 和 x86 静态库。
- 按提交号取 ggml、librime、librime-lua（含 thirdparty 分支）、librime-octagram。
- 克隆 nova-windows 和 nova-macos 的 nova 分支（nova-lm、nova-ui 的源码在 nova-macos）。
- 编 ggml，再调用 `build.bat` 编一遍全部程序。

## 3. 日常编译

在 Mac 上运行 `nova/package.sh`：它把 Mac 上的工作副本拷过去，调用 `build.bat`，取回程序打安装包。也可以在 Windows 上直接运行 `build.bat`，最后一行是 `[build] BUILD_OK` 就成了，日志在 `C:\nova\build-*.log`。

## 注意

- `.bat` 文件只能用 ASCII：cmd 按控制台代码页（中文系统是 GBK）解析批处理，UTF-8 的中文注释会被拆成命令执行。
- 升级依赖：改 `versions.bat`，删掉 `C:\nova` 下对应的目录，重跑 `setup.bat`。
