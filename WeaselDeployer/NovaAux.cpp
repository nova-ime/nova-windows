#include "stdafx.h"
#include "NovaAux.h"
#include "NovaAuxGen.h"
#include <WeaselIPC.h>
#include <WeaselUtility.h>
#include <NovaOptions.h>
#include <rime_api.h>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace nova_aux {

const Scheme kSchemes[kSchemeCount] = {
    {L"zrm", L"自然码形码", "自然码"},
    {L"flypy", L"小鹤形码", "小鹤"},
    {L"moqi", L"墨奇码", "墨奇"},
};

const wchar_t* const kStages[kStageCount] = {
    L"生成词库", L"读取词条", L"整理词条", L"写入词库", L"重新加载"};

namespace {

// 各阶段在总进度里的起点。Windows 上实测（小鹤）：读取 13 秒、整理 26 秒、写入 9 秒；
// 生成和重新部署各约几秒。
constexpr double kStarts[kStageCount + 1] = {0, 0.05, 0.27, 0.72, 0.87, 1.0};

// prism（拼写索引）要和词表一起换：librime 找不到词库源文件时不核对 prism 是按哪张词表建的，
// 留着旧的会把编码对到错的词上，打出来全是乱字。
const wchar_t* const kTables[] = {L"nova_wx.table.bin", L"nova_wx.reverse.bin",
                                  L"nova_wx.table.txt", L"nova.prism.bin",
                                  L"nova.prism.txt"};
const wchar_t* const kSources[] = {L"nova_wx.dict.yaml", L"nova_wx_jp.dict.yaml"};

fs::path aux_dir() {
  return WeaselSharedDataPath() / L"nova_aux";
}

// 词条表随安装包升级可能变；记下生成时用的那份的大小和修改时间，对不上就要重新生成。
std::wstring base_stamp() {
  std::error_code ec;
  const fs::path base = aux_dir() / L"nova_wx.base.txt";
  const auto size = fs::file_size(base, ec);
  if (ec)
    return L"";
  const auto time = fs::last_write_time(base, ec);
  if (ec)
    return L"";
  return std::to_wstring(size) + L"-" +
         std::to_wstring(time.time_since_epoch().count());
}

void remove_quietly(const fs::path& p) {
  std::error_code ec;
  fs::remove(p, ec);
}

}  // namespace

std::wstring current() {
  return nova::aux();
}

std::wstring scheme_name(const std::wstring& id) {
  for (const auto& s : kSchemes)
    if (id == s.id)
      return s.name;
  return kSchemes[0].name;
}

bool needs_rebuild() {
  return current() != L"zrm" &&
         nova::reg_string(L"NovaAuxStamp") != base_stamp();
}

bool rebuild_if_needed() {
  if (!needs_rebuild())
    return true;
  LOG(INFO) << "Nova: word table is out of date, regenerating for "
            << wtou8(current());
  Builder builder;
  const bool ok = builder.run(current());
  if (!ok)
    LOG(ERROR) << "Nova: regenerating word table failed: "
               << wtou8(builder.error());
  return ok;
}

// MARK: - Builder

bool Builder::running() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_;
}

int Builder::stage() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stage_;
}

// 整理词条阶段日志里没有中间进度，按预计时间往前走，最多走到这一段的 95%。
double Builder::fraction() const {
  std::lock_guard<std::mutex> lock(mutex_);
  double f = fraction_;
  if (running_ && stage_ == 2 && sorting_) {
    const double t =
        std::chrono::duration<double>(Clock::now() - sort_began_).count();
    const double a = kStarts[2], b = kStarts[3];
    f = (std::max)(f, a + (b - a) * (std::min)(t / sort_expected_, 0.95));
  }
  return f;
}

double Builder::elapsed() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto to = running_ ? Clock::now() : finished_;
  return std::chrono::duration<double>(to - began_).count();
}

std::wstring Builder::error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

void Builder::begin() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = true;
  stage_ = 0;
  fraction_ = 0;
  began_ = finished_ = Clock::now();
  reading_ = sorting_ = false;
  sort_expected_ = 20;
  passes_ = 0;
  tail_.clear();
  error_.clear();
}

void Builder::end() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  finished_ = Clock::now();
  if (error_.empty()) {
    stage_ = kStageCount;
    fraction_ = 1;
  }
}

void Builder::set(int stage, double within) {
  std::lock_guard<std::mutex> lock(mutex_);
  stage_ = stage;
  within = (std::min)((std::max)(within, 0.0), 1.0);
  const double a = kStarts[stage], b = kStarts[stage + 1];
  fraction_ = (std::max)(fraction_, a + (b - a) * within);
}

bool Builder::fail(const std::wstring& message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = message;
  }
  end();
  return false;
}

bool Builder::run(const std::wstring& scheme) {
  begin();
  const fs::path user = WeaselUserDataPath();
  const fs::path shared = WeaselSharedDataPath();
  const fs::path build = user / L"build";
  std::error_code ec;

  if (scheme == L"zrm") {
    // 回到预编译的词表和 prism：删掉用户目录里的。
    set(4);
    const bool ok = maintain([&] {
      for (const auto* f : kTables)
        remove_quietly(build / f);
      for (const auto* f : kSources)
        remove_quietly(user / f);
      return true;
    });
    if (!ok)
      return false;
    nova::set_reg_string(L"NovaAux", L"");
    nova::set_reg_string(L"NovaAuxStamp", L"");
    end();
    return true;
  }

  const Scheme* found = nullptr;
  for (const auto& s : kSchemes)
    if (scheme == s.id)
      found = &s;
  if (!found)
    return fail(L"不认识的辅助码：" + scheme);

  set(0);
  fs::create_directories(user, ec);
  std::string missing;
  const bool generated = generate(
      aux_dir() / L"nova_wx.base.txt", aux_dir() / (scheme + L".txt"),
      found->label, user / kSources[0], user / kSources[1],
      [this](double p) { set(0, p); }, &missing);
  auto drop_sources = [&] {
    for (const auto* f : kSources)
      remove_quietly(user / f);
  };
  if (!generated) {
    drop_sources();
    return fail(L"打不开文件：" + u8tow(missing) + L"。请重新安装星语。");
  }

  // 编译到临时目录，输入法照常用 build\ 里的旧词表。
  const fs::path staging = user / L"build.aux-tmp";
  fs::remove_all(staging, ec);
  fs::create_directories(staging, ec);
  if (!compile(user.wstring(), shared.wstring(), staging.wstring())) {
    drop_sources();
    fs::remove_all(staging, ec);
    return false;  // compile 已写好错误
  }

  set(4);
  std::wstring swap_error;
  const bool ok = maintain([&] {
    std::error_code e;
    fs::create_directories(build, e);
    for (const auto* f : kTables) {
      const fs::path from = staging / f;
      if (!fs::exists(from, e))
        continue;
      if (!MoveFileExW(from.c_str(), (build / f).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        swap_error = L"换不进词表：" + std::wstring(f) + L"（错误 " +
                     std::to_wstring(GetLastError()) + L"）";
        break;
      }
    }
    // 不论换没换成都删掉源文件：留着会让以后每次部署都重新编译整套词库。
    drop_sources();
    return swap_error.empty();
  });
  fs::remove_all(staging, ec);
  if (!swap_error.empty())
    return fail(swap_error);
  if (!ok)
    return false;

  nova::set_reg_string(L"NovaAux", scheme);
  nova::set_reg_string(L"NovaAuxStamp", base_stamp());
  end();
  return true;
}

bool Builder::reload() {
  begin();
  set(4);
  if (!maintain([] { return true; }))
    return false;
  end();
  return true;
}

bool Builder::maintain(const std::function<bool()>& swap) {
  HANDLE mutex = CreateMutex(NULL, TRUE, L"WeaselDeployerMutex");
  if (!mutex)
    return fail(L"无法开始部署。");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return fail(L"正在进行另一项部署，请稍后再试。");
  }

  weasel::Client client;
  if (client.Connect()) {
    LOG(INFO) << "Nova: turning WeaselServer into maintenance mode.";
    client.StartMaintenance();
  }
  const bool swapped = swap();
  {
    RimeApi* rime = rime_get_api();
    rime->deploy();
    rime->deploy_config_file("weasel.yaml", "config_version");
  }
  CloseHandle(mutex);  // 恢复服务前要放开
  if (client.Connect()) {
    LOG(INFO) << "Nova: resuming service.";
    client.EndMaintenance();
  }
  return swapped;
}

// MARK: - 编译

namespace {

std::wstring quote(const std::wstring& s) {
  return L"\"" + s + L"\"";
}

}  // namespace

bool Builder::compile(const std::wstring& user,
                      const std::wstring& shared,
                      const std::wstring& staging) {
  const fs::path exe = fs::path(shared).parent_path() / L"rime_deployer.exe";
  if (!fs::exists(exe))
    return fail(L"缺少 rime_deployer.exe。请重新安装星语。");

  // 参数：--compile <方案> <用户目录> <共享目录> <输出目录>。
  // 目录末尾的反斜杠会把后面的引号转义掉，所以都不带结尾斜杠。
  std::wstring cmd = quote(exe.wstring()) + L" --compile " +
                     quote((fs::path(shared) / L"nova.schema.yaml").wstring()) +
                     L" " + quote(user) + L" " + quote(shared) + L" " +
                     quote(staging);

  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
  HANDLE read_end = NULL, write_end = NULL;
  if (!CreatePipe(&read_end, &write_end, &sa, 0))
    return fail(L"无法启动编译。");
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si = {sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = write_end;
  si.hStdError = write_end;
  PROCESS_INFORMATION pi = {};

  // glog 默认写日志文件；要它写到 stderr，才能从管道里读进度。子进程继承这个环境变量。
  SetEnvironmentVariableW(L"GLOG_logtostderr", L"1");
  std::vector<wchar_t> line(cmd.begin(), cmd.end());
  line.push_back(0);
  const BOOL started = CreateProcessW(
      exe.c_str(), line.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
      fs::path(shared).parent_path().c_str(), &si, &pi);
  SetEnvironmentVariableW(L"GLOG_logtostderr", NULL);
  CloseHandle(write_end);
  if (!started) {
    CloseHandle(read_end);
    return fail(L"无法启动 rime_deployer.exe（错误 " +
                std::to_wstring(GetLastError()) + L"）。");
  }

  std::string pending;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(read_end, buf, sizeof(buf), &n, NULL) && n > 0) {
    pending.append(buf, n);
    size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
      std::string one = pending.substr(0, nl);
      pending.erase(0, nl + 1);
      if (!one.empty() && one.back() == '\r')
        one.pop_back();
      parse(one);
    }
  }
  CloseHandle(read_end);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (code != 0) {
    std::string tail;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tail = tail_;
    }
    return fail(L"编译词库失败（退出码 " + std::to_wstring(code) + L"）。" +
                u8tow(tail));
  }
  return true;
}

// 按 librime 编译词库时的日志推进度（dict_compiler.cc、entry_collector.cc、table.cc）。
void Builder::parse(const std::string& line) {
  if (!line.empty() && line[0] == 'E') {
    std::lock_guard<std::mutex> lock(mutex_);
    tail_ = line;
  }
  if (line.find("collecting entries from") != std::string::npos) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!reading_) {
        reading_ = true;
        read_began_ = Clock::now();
      }
    }
    set(1, passes_ / 2.0);
  } else if (line.find("Pass 1: total ") != std::string::npos) {
    ++passes_;
    set(1, passes_ / 2.0);
  } else if (line.find("Pass 3:") != std::string::npos) {
    {
      // 整理用时约是读取的 2 到 3 倍（Mac 3.2 → 9.3 秒，Windows 13 → 26 秒），按 2.5 倍估。
      std::lock_guard<std::mutex> lock(mutex_);
      sorting_ = true;
      sort_began_ = Clock::now();
      if (reading_)
        sort_expected_ = (std::max)(
            3.0, 2.5 * std::chrono::duration<double>(sort_began_ - read_began_)
                           .count());
    }
    set(2);
  } else if (line.find("building table.") != std::string::npos) {
    set(3);
  } else if (line.find("building prism") != std::string::npos ||
             line.find("building reversedb") != std::string::npos) {
    set(3, 0.5);
  }
}

}  // namespace nova_aux
