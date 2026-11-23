// 不用 stdafx.h：这个文件只用标准库，在 vcxproj 里关掉了预编译头，Mac 上也能单独编译测试：
//   clang++ -std=c++17 -O2 NovaAuxGen.cpp test_main.cpp
#include "NovaAuxGen.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace nova_aux {

namespace {

// 解一个 UTF-8 字符，返回码位，p 前移。坏字节当作单字节处理。
uint32_t next_scalar(const char*& p, const char* end) {
  const auto c = static_cast<unsigned char>(*p++);
  int extra = 0;
  uint32_t v = c;
  if (c >= 0xF0) {
    extra = 3;
    v = c & 0x07;
  } else if (c >= 0xE0) {
    extra = 2;
    v = c & 0x0F;
  } else if (c >= 0xC0) {
    extra = 1;
    v = c & 0x1F;
  }
  for (int i = 0; i < extra && p < end; ++i)
    v = (v << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
  return v;
}

bool read_file(const std::filesystem::path& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

std::string header(const std::string& name, const std::string& label, const char* imports) {
  std::string h = u8"# 由星语按" + label + u8"辅助码生成（词库来自万象拼音，CC-BY-4.0），不要手改。\n";
  h += "---\nname: " + name + "\nversion: \"wx\"\nsort: by_weight\nuse_preset_vocabulary: false\n";
  if (imports)
    h += std::string("import_tables:\n  - ") + imports + "\n";
  h += "...\n";
  return h;
}

constexpr int kMaxCombos = 4;

}  // namespace

bool generate(const std::filesystem::path& base_path,
              const std::filesystem::path& aux_path,
              const std::string& label,
              const std::filesystem::path& main_path,
              const std::filesystem::path& jp_path,
              const std::function<void(double)>& progress,
              std::string* error) {
  std::string base, aux_text;
  if (!read_file(base_path, &base)) {
    *error = base_path.filename().string();
    return false;
  }
  if (!read_file(aux_path, &aux_text)) {
    *error = aux_path.filename().string();
    return false;
  }

  // 「字 → 形码」表。
  std::unordered_map<uint32_t, std::vector<std::string>> aux;
  {
    const char* p = aux_text.data();
    const char* end = p + aux_text.size();
    while (p < end) {
      const char* nl = p;
      while (nl < end && *nl != '\n')
        ++nl;
      const char* tab = p;
      while (tab < nl && *tab != '\t')
        ++tab;
      if (tab > p && tab < nl) {
        const char* q = p;
        const uint32_t ch = next_scalar(q, tab);
        auto& codes = aux[ch];
        codes.clear();
        const char* s = tab + 1;
        while (true) {
          const char* comma = s;
          while (comma < nl && *comma != ',')
            ++comma;
          codes.emplace_back(s, comma);
          if (comma >= nl)
            break;
          s = comma + 1;
        }
      }
      p = nl + 1;
    }
  }

  std::ofstream main_file(main_path, std::ios::binary | std::ios::trunc);
  std::ofstream jp_file(jp_path, std::ios::binary | std::ios::trunc);
  if (!main_file || !jp_file) {
    *error = (main_file ? jp_path : main_path).filename().string();
    return false;
  }

  std::string out, jp;
  out.reserve(1 << 23);
  jp.reserve(1 << 22);
  out += header("nova_wx", label, "nova_wx_jp");
  jp += header("nova_wx_jp", label, nullptr);

  const char* const data = base.data();
  const size_t total = base.size();
  size_t start = 0, last_report = 0;
  std::vector<uint32_t> scalars;
  std::vector<std::vector<std::string>> alts;
  std::vector<size_t> index;
  while (start < total) {
    size_t end = start;
    while (end < total && data[end] != '\n')
      ++end;
    // 词\t音节连写\t词频
    size_t t1 = start;
    while (t1 < end && data[t1] != '\t')
      ++t1;
    size_t t2 = t1 + 1;
    while (t2 < end && data[t2] != '\t')
      ++t2;
    if (t2 >= end) {  // 不成行，跳过
      start = end + 1;
      continue;
    }
    const std::string word(data + start, t1 - start);
    const char* sps = data + t1 + 1;
    const size_t sps_len = t2 - t1 - 1;
    const std::string weight(data + t2 + 1, end - t2 - 1);

    // 按音节查形码用的字：间隔号「·」（U+00B7）不占音节。
    scalars.clear();
    for (const char *p = word.data(), *e = p + word.size(); p < e;) {
      const uint32_t s = next_scalar(p, e);
      if (s != 0xB7)
        scalars.push_back(s);
    }
    const size_t n = sps_len / 2;
    alts.assign(n, {});
    for (size_t i = 0; i < n; ++i) {
      const std::string sp(sps + 2 * i, 2);
      auto it = i < scalars.size() ? aux.find(scalars[i]) : aux.end();
      if (it != aux.end()) {
        for (const auto& code : it->second)
          alts[i].push_back(sp + ";" + code);
      } else {
        alts[i].push_back(sp);
      }
    }
    // 和 itertools.product 同样的顺序：最右边的音节变得最快。最多 4 条。
    index.assign(n, 0);
    for (int written = 0; written < kMaxCombos; ++written) {
      out += word;
      out += '\t';
      for (size_t i = 0; i < n; ++i) {
        if (i > 0)
          out += ' ';
        out += alts[i][index[i]];
      }
      out += '\t';
      out += weight;
      out += '\n';
      long k = static_cast<long>(n) - 1;
      while (k >= 0) {
        if (++index[k] < alts[k].size())
          break;
        index[k] = 0;
        --k;
      }
      if (k < 0)
        break;
    }
    // 简拼表：词频取开头的数字（万象有几条写成 6N），是 0 的不收。
    long long count = 0;
    for (char c : weight) {
      if (c < '0' || c > '9')
        break;
      count = count * 10 + (c - '0');
    }
    if (count > 0) {
      jp += word;
      jp += '\t';
      for (size_t i = 0; i < n; ++i) {
        if (i > 0)
          jp += ' ';
        jp.append(sps + 2 * i, 2);
        jp += '~';
      }
      jp += '\t';
      jp += std::to_string(count);
      jp += '\n';
    }

    if (out.size() > (1u << 23)) {
      main_file.write(out.data(), out.size());
      out.clear();
    }
    if (jp.size() > (1u << 22)) {
      jp_file.write(jp.data(), jp.size());
      jp.clear();
    }
    if (end - last_report > (1u << 20)) {
      progress(static_cast<double>(end) / total);
      last_report = end;
    }
    start = end + 1;
  }
  main_file.write(out.data(), out.size());
  jp_file.write(jp.data(), jp.size());
  main_file.close();
  jp_file.close();
  if (!main_file || !jp_file) {
    *error = (main_file ? jp_path : main_path).filename().string();
    return false;
  }
  progress(1);
  return true;
}

}  // namespace nova_aux
