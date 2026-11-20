// Offline renderer for the Nova candidate panel: renders scenes (same content as the
// Mac harness .scratch/bench/nova-ui/harness/main.swift) to PNG with the same
// NovaRenderer code the panel uses.
// usage: nova_shot.exe <out-dir> [font-px=20] [face="Microsoft YaHei UI"]
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../WeaselUI/NovaRenderer.h"

using Microsoft::WRL::ComPtr;
using namespace weasel;

static const char* US = "\x1f";

static std::string Line(const std::string& flags, int from, int to, const std::string& key,
                        const std::string& word, int share,
                        const std::vector<std::pair<std::string, int>>& alts) {
  std::string s = flags + "\t" + std::to_string(from) + "\t" + std::to_string(to) + "\t" + key + "\t" + word;
  if (share >= 0) s += US + std::to_string(share);
  for (auto& a : alts) {
    s += "\t" + a.first;
    if (a.second >= 0) s += US + std::to_string(a.second);
  }
  return s;
}
static std::string Tree(const std::vector<std::string>& lines) {
  std::string s;
  for (auto& l : lines) s += l + "\n";
  return s;
}

struct Cand { std::string label, text, comment; };

struct Scene {
  std::string name;
  std::string tree;  // parsed into blocks; empty = no blocks
  std::string preedit;
  int sel_loc = -1, sel_len = 0;
  std::string letters;
  int cursor = 0;
  std::vector<int> letter_map;
  std::vector<Cand> cands;
  int highlighted = 0;
  bool linear_too = false;
};

// Owns everything a NovaUIContent points into.
struct Built {
  NovaUITree* tree = nullptr;
  std::vector<NovaUICandidate> cands;
  NovaUIContent c{};
  ~Built() { if (tree) nova_ui_tree_free(tree); }
};

static void BuildContent(const Scene& sc, Built& b) {
  b.c = NovaUIContent{};
  b.c.preedit = sc.preedit.c_str();
  b.c.sel_location = sc.sel_loc;
  b.c.sel_length = sc.sel_len;
  if (!sc.tree.empty()) {
    b.tree = nova_ui_tree_parse(sc.tree.c_str());
    b.c.blocks = nova_ui_tree_blocks(b.tree, &b.c.block_count);
  }
  b.c.letters = sc.letters.c_str();
  b.c.cursor = sc.cursor;
  b.c.letter_map = sc.letter_map.empty() ? nullptr : sc.letter_map.data();
  b.c.letter_map_count = (int)sc.letter_map.size();
  for (auto& c : sc.cands) b.cands.push_back({c.label.c_str(), c.text.c_str(), c.comment.c_str()});
  b.c.candidates = b.cands.data();
  b.c.candidate_count = (int)b.cands.size();
  b.c.highlighted = sc.highlighted;
}

static std::vector<Scene> Scenes() {
  using A = std::vector<std::pair<std::string, int>>;
  std::string noFocus = Tree({Line("0", 0, 2, "wo", u8"\u6211", 62, A{{u8"\u7a9d", 20}, {u8"\u6c83", 10}}),
                              Line("0", 2, 7, "xihuan", u8"\u559c\u6b22", 80, A{{u8"\u6d17\u6362", 5}}),
                              Line("0", 7, 10, "chi", u8"\u5403", 55, A{{u8"\u6301", 20}, {u8"\u6c60", 10}, {u8"\u8fdf", 8}}),
                              Line("0", 10, 14, "pingguo", u8"\u82f9\u679c", -1, A{})});
  std::string focus = Tree({Line("0", 0, 2, "wo", u8"\u6211", 62, A{{u8"\u7a9d", 20}, {u8"\u6c83", 10}}),
                            Line("0f", 2, 7, "xihuan", u8"\u559c\u6b22", 80, A{{u8"\u6d17\u6362", 5}, {u8"\u897f\u6b22", 3}, {u8"\u7cfb\u8fd8", 2}, {u8"\u620f\u6362", 1}}),
                            Line("0", 7, 10, "chi", u8"\u5403", 55, A{{u8"\u6301", 20}})});
  std::string overview = Tree({Line("0", 0, 2, "wo", u8"\u6211", -1, A{}),
                               Line("0uo", 2, 5, "xia", u8"\u4e0b", 40, A{{u8"\u590f", 30}, {u8"\u5413", 12}, {u8"\u5ce1", 4}}),
                               Line("0", 5, 9, "yuan", u8"\u56ed", 70, A{{u8"\u5706", 10}}),
                               Line("0uo", 9, 13, "gong", u8"\u5de5", 33, A{{u8"\u516c", 31}, {u8"\u529f", 20}})});
  std::string mixed = Tree({Line("1", 0, 2, "wo", u8"\u6211", 62, A{{u8"\u7a9d", 20}}),
                            Line("0c", 2, 7, "xihuan", u8"\u559c\u6b22", 80, A{{u8"\u6d17\u6362", 5}}),
                            Line("0u", 7, 10, "chi", u8"\u5403", 55, A{}),
                            Line("1f", 10, 14, "pingguo", u8"\u82f9\u679c", 90, A{{u8"\u5e73\u679c", 5}, {u8"\u74f6\u679c", 2}})});
  std::string paged = Tree({Line("0", 0, 2, "wo", u8"\u6211", 62, A{{u8"\u7a9d", 20}}),
                            Line("0fp2/5h3", 2, 4, "shi", u8"\u662f", 80, A{{u8"\u65f6", 30}, {u8"\u4e8b", 20}, {u8"\u5341", 10}, {u8"\u5e02", 5}, {u8"\u4f7f", 3}, {u8"\u5f0f", 2}})});
  std::vector<std::string> longLines;
  for (int i = 0; i < 9; i++)
    longLines.push_back(Line(i == 4 ? "0fp1/2" : "0", i * 3, i * 3 + 3, "k" + std::to_string(i),
                             u8"\u6d4b\u8bd5\u8bcd\u5757" + std::to_string(i), 50 + i,
                             i % 2 == 0 ? A{{u8"\u5907\u9009\u7532", 30}, {u8"\u5907\u9009\u4e59\u4e59", 10}} : A{}));
  std::string tlong = Tree(longLines);
  std::string english = Tree({Line("0o", 0, 5, "hello", "hello", -1, A{{"Hello", -1}, {"HELLO", -1}}),
                              Line("0", 5, 11, u8"\u4e16\u754c", "world", 10, A{})});
  std::vector<Cand> list = {{"1", u8"\u4f60\u597d", ""}, {"2", u8"\u4f60\u53f7", u8"n\u01d0 h\u00e0o"},
                            {"3", u8"\u62df\u597d", ""}, {"4", u8"\u6ce5\u6d69", ""}, {"5", u8"\u5c3c\u660a", "rare"}};
  std::vector<Scene> v;
  auto add = [&](Scene s) { v.push_back(std::move(s)); };
  { Scene s; s.name = "status"; s.preedit = u8"\u5df2\u5207\u6362\u5230\u5168\u62fc"; s.linear_too = true; add(s); }
  { Scene s; s.name = "tree-nofocus-letters"; s.tree = noFocus; s.letters = "woxihuanchipingguo"; s.cursor = 18; add(s); }
  { Scene s; s.name = "tree-focus-letters"; s.tree = focus; s.letters = "woxihuanchi"; s.cursor = 7; add(s); }
  { Scene s; s.name = "tree-overview-orange"; s.tree = overview; s.letters = "woxiayuangong"; s.cursor = 13; add(s); }
  { Scene s; s.name = "tree-mixed-colors"; s.tree = mixed; s.letters = "woxihuanchipingguo"; s.cursor = 14; add(s); }
  { Scene s; s.name = "tree-paged"; s.tree = paged; s.letters = "woshi"; s.cursor = 4; add(s); }
  { Scene s; s.name = "tree-long"; s.tree = tlong; s.letters = "abcdefghijklmnopqrstuvwxyz0"; s.cursor = 12; add(s); }
  { Scene s; s.name = "tree-english-preedit"; s.tree = english; s.preedit = u8"hello\u4e16\u754c"; s.sel_loc = 5; s.sel_len = 2; add(s); }
  { Scene s; s.name = "tree-quanpin"; s.tree = focus; s.letters = "wo'xihuan'chi"; s.cursor = 5;
    s.letter_map = {0, 2, 3, 6, 7, 10, 11, 12, 13}; add(s); }
  { Scene s; s.name = "one-letter"; s.tree = Tree({Line("0", 0, 1, "w", u8"我", 62, A{{u8"为", 20}, {u8"外", 10}})});
    s.letters = "w"; s.cursor = 1; add(s); }
  { Scene s; s.name = "one-letter-bare"; s.letters = "w"; s.cursor = 1; add(s); }
  { Scene s; s.name = "list-basic"; s.cands = list; s.preedit = "nihao"; s.sel_loc = 0; s.sel_len = 5; s.highlighted = 0; s.linear_too = true; add(s); }
  { Scene s; s.name = "list-highlight2"; s.cands = list; s.preedit = u8"\u4f60hao"; s.sel_loc = 1; s.sel_len = 3; s.highlighted = 2; s.linear_too = true; add(s); }
  { Scene s; s.name = "list-long"; for (int i = 0; i < 14; i++)
      s.cands.push_back({std::to_string(i % 9 + 1), u8"\u5019\u9009\u8bcd\u8bed" + std::to_string(i), i % 3 == 0 ? u8"\u6ce8" : ""});
    s.preedit = "houxuan"; s.highlighted = 3; s.linear_too = true; add(s); }
  return v;
}

static const struct { const char* tag; double r, g, b; } kBackdrops[] = {{"light", 0.94, 0.94, 0.94}, {"dark", 0.10, 0.10, 0.12}};

static bool SavePng(IWICImagingFactory* wic, IWICBitmap* bmp, const std::wstring& path) {
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> enc;
  ComPtr<IWICBitmapFrameEncode> frame;
  UINT w, h;
  bmp->GetSize(&w, &h);
  if (FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
  if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) || FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return false;
  if (FAILED(enc->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr))) return false;
  frame->SetSize(w, h);
  WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
  frame->SetPixelFormat(&fmt);
  if (FAILED(frame->WriteSource(bmp, nullptr))) return false;
  return SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 2) { fprintf(stderr, "usage: nova_shot <out-dir> [font-px] [face]\n"); return 2; }
  std::wstring out = argv[1];
  double fontPx = argc > 2 ? _wtof(argv[2]) : 20.0;
  std::wstring face = argc > 3 ? argv[3] : L"Microsoft YaHei UI";
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComPtr<ID2D1Factory> d2d;
  ComPtr<IDWriteFactory> dw;
  ComPtr<IWICImagingFactory> wic;
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
  CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
  if (!d2d || !dw || !wic) { fprintf(stderr, "factory init failed\n"); return 1; }
  CreateDirectoryW(out.c_str(), nullptr);

  struct Variant { std::string tag; double width; bool upward, share, linear; float scale; };
  int count = 0, failed = 0;
  for (const Scene& sc : Scenes()) {
    std::vector<Variant> variants = {{"", 900, false, false, false, 1.0f},
                                     {"_up", 900, true, false, false, 1.0f},
                                     {"_narrow", 220, false, false, false, 1.0f},
                                     {"_share", 900, false, true, false, 1.0f},
                                     {"_x2", 900, false, false, false, 2.0f}};
    if (sc.linear_too) variants.push_back({"_linear", 900, false, false, true, 1.0f});
    for (const Variant& var : variants) {
      Built built;
      BuildContent(sc, built);
      NovaRenderer r;
      NovaRenderer::Style st;
      st.face = face;
      st.font_size = fontPx;
      st.linear = var.linear;
      st.show_share = var.share;
      if (!r.BuildContent(dw.Get(), &built.c, st, var.width, var.upward)) { printf("skip (empty) %s\n", sc.name.c_str()); continue; }
      const int pad = 12;
      UINT w = (UINT)(r.width() * var.scale + 0.999) + 2 * pad, h = (UINT)(r.height() * var.scale + 0.999) + 2 * pad;
      for (auto& bd : kBackdrops) {
        ComPtr<IWICBitmap> bmp;
        wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
        ComPtr<ID2D1RenderTarget> rt;
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) { failed++; continue; }
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF((float)bd.r, (float)bd.g, (float)bd.b, 1.0f));
        rt->SetTransform(D2D1::Matrix3x2F::Translation((float)pad, (float)pad));
        r.Paint(rt.Get(), var.scale, -1);
        if (FAILED(rt->EndDraw())) { failed++; continue; }
        std::string name = sc.name + var.tag + "_" + bd.tag + ".png";
        std::wstring path = out + L"\\" + std::wstring(name.begin(), name.end());
        if (SavePng(wic.Get(), bmp.Get(), path)) { count++; } else { failed++; fprintf(stderr, "save failed %s\n", name.c_str()); }
      }
      printf("%s%s  %.1f x %.1f\n", sc.name.c_str(), var.tag.c_str(), r.width(), r.height());
    }
  }
  printf("wrote %d png, %d failed\n", count, failed);
  return failed ? 1 : 0;
}
