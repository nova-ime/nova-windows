#include "NovaRenderer.h"

#include <algorithm>
#include <cmath>
#include <map>

using Microsoft::WRL::ComPtr;

namespace weasel {

namespace {

std::string ToUtf8(const std::wstring& w) {
  if (w.empty())
    return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr,
                      nullptr);
  return s;
}

std::wstring ToWide(const char* s) {
  if (!s || !*s)
    return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 1)
    return std::wstring();
  std::wstring w(n - 1, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
  return w;
}

// Font family for the body: first entry of "A, B:range, C", without ranges.
std::wstring FirstFace(const std::wstring& faces) {
  size_t end = faces.find_first_of(L",:");
  std::wstring f = faces.substr(0, end);
  size_t a = f.find_first_not_of(L" \t");
  size_t b = f.find_last_not_of(L" \t");
  if (a == std::wstring::npos)
    return std::wstring();
  return f.substr(a, b - a + 1);
}

D2D1_COLOR_F ColorOf(NovaUIColor token) {
  double c[4];
  nova_ui_default_color(token, c);
  return D2D1::ColorF((float)c[0], (float)c[1], (float)c[2], (float)c[3]);
}

}  // namespace

struct NovaRenderer::Impl {
  IDWriteFactory* dwrite = nullptr;
  Style style;
  ComPtr<IDWriteTextFormat> formats[4];
  NovaUIContentHandle* content = nullptr;
  NovaUILayout* layout = nullptr;
  const NovaUILayoutView* view = nullptr;
  const NovaUIContent* source = nullptr;

  void ReleaseLayout() {
    if (layout)
      nova_ui_layout_free(layout);
    if (content)
      nova_ui_content_free(content);
    layout = nullptr;
    content = nullptr;
    view = nullptr;
    source = nullptr;
  }

  IDWriteTextFormat* Format(NovaUIFont role) {
    int i = (int)role;
    if (!formats[i]) {
      std::wstring face = style.face;
      DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
      if (role != NOVA_FONT_BODY) {
        face = L"Consolas";
        if (role == NOVA_FONT_LABEL)
          weight = DWRITE_FONT_WEIGHT_MEDIUM;
      } else if (face.empty()) {
        face = L"Microsoft YaHei UI";
      }
      float size = (float)nova_ui_font_size(role, style.font_size);
      HRESULT hr = dwrite->CreateTextFormat(
          face.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn", &formats[i]);
      if (FAILED(hr))
        return nullptr;
      formats[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    return formats[i].Get();
  }

  ComPtr<IDWriteTextLayout> TextLayout(const std::wstring& text,
                                       NovaUIFont role) {
    ComPtr<IDWriteTextLayout> tl;
    IDWriteTextFormat* fmt = Format(role);
    if (!fmt)
      return tl;
    dwrite->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, 100000.0f,
                             100000.0f, &tl);
    return tl;
  }
};

static double MeasureCallback(const char* text, NovaUIFont font, void* user) {
  auto* impl = static_cast<NovaRenderer::Impl*>(user);
  std::wstring w = ToWide(text);
  if (w.empty())
    return 0;
  auto tl = impl->TextLayout(w, font);
  if (!tl)
    return 0;
  DWRITE_TEXT_METRICS m{};
  tl->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

NovaRenderer::NovaRenderer() : impl_(new Impl) {}
NovaRenderer::~NovaRenderer() {
  Reset();
  delete impl_;
}

void NovaRenderer::Reset() {
  impl_->ReleaseLayout();
  width_ = height_ = 0;
}

double NovaRenderer::FullHeight(const Style& style) {
  NovaUIStyle s = {style.font_size, style.linear ? 1 : 0,
                   style.show_share ? 1 : 0};
  return nova_ui_full_height(&s);
}

bool NovaRenderer::Build(IDWriteFactory* dwrite,
                         const Context& ctx,
                         const Style& style,
                         double max_width,
                         bool upward) {
  Reset();
  if (!dwrite)
    return false;
  Impl& d = *impl_;

  // --- assemble content from the context (strings stay alive in this scope) ---
  const NovaData& nv = ctx.nova;
  std::string input = ToUtf8(nv.input), tree = ToUtf8(nv.tree),
              list = ToUtf8(nv.list), cursor = ToUtf8(nv.cursor),
              raw = ToUtf8(nv.qp_raw), map = ToUtf8(nv.qp_map),
              preedit = ToUtf8(ctx.preedit.str);
  const auto& ci = ctx.cinfo;
  size_t count = ci.candies.size();
  std::vector<std::string> texts(count), comments(count), labels(count);
  std::vector<const char*> ptexts(count), pcomments(count), plabels(count);
  for (size_t i = 0; i < count; i++) {
    texts[i] = ToUtf8(ci.candies[i].str);
    comments[i] = i < ci.comments.size() ? ToUtf8(ci.comments[i].str) : "";
    labels[i] = i < ci.labels.size() ? ToUtf8(ci.labels[i].str)
                                     : std::to_string(i + 1);
    ptexts[i] = texts[i].c_str();
    pcomments[i] = comments[i].c_str();
    plabels[i] = labels[i].c_str();
  }
  NovaUIAssembleInput in = {};
  in.input = input.c_str();
  in.prop_tree = nv.has_tree ? tree.c_str() : nullptr;
  in.prop_list = nv.has_list ? list.c_str() : nullptr;
  in.prop_cursor = nv.has_cursor ? cursor.c_str() : nullptr;
  in.prop_qp_raw = nv.has_raw ? raw.c_str() : nullptr;
  in.prop_qp_map = nv.has_map ? map.c_str() : nullptr;
  in.page_no = ci.currentPage;
  in.highlighted = ci.highlighted;
  in.num_candidates = (int)count;
  in.preedit = preedit.c_str();
  in.sel_location = -1;
  in.sel_length = 0;
  for (const auto& a : ctx.preedit.attributes) {
    if (a.type == HIGHLIGHTED) {
      in.sel_location = a.range.start;
      in.sel_length = a.range.end - a.range.start;
      break;
    }
  }
  in.inline_preedit = style.inline_preedit ? 1 : 0;
  in.count = (int)count;
  in.texts = ptexts.data();
  in.comments = pcomments.data();
  in.labels = plabels.data();
  in.label_count = (int)count;

  d.content = nova_ui_assemble(&in);
  if (!d.content)
    return false;
  return BuildContent(dwrite, nova_ui_content_view(d.content), style,
                      max_width, upward);
}

bool NovaRenderer::BuildContent(IDWriteFactory* dwrite,
                                const NovaUIContent* view,
                                const Style& style,
                                double max_width,
                                bool upward) {
  Impl& d = *impl_;
  if (!dwrite || !view)
    return false;
  if (d.dwrite != dwrite || d.style.face != style.face ||
      d.style.font_size != style.font_size) {
    for (auto& f : d.formats)
      f.Reset();
  }
  d.dwrite = dwrite;
  d.style = style;
  d.source = view;
  // The letters row is only laid out next to word blocks; with no blocks,
  // no candidates and no preedit there is nothing to draw but the empty card
  // (a small dark dot after the first letter), so show nothing.
  if (view->block_count == 0 && view->candidate_count == 0 &&
      (!view->preedit || !*view->preedit)) {
    Reset();
    return false;
  }
  if (d.layout) {
    nova_ui_layout_free(d.layout);
    d.layout = nullptr;
    d.view = nullptr;
  }
  NovaUIStyle s = {style.font_size, style.linear ? 1 : 0,
                   style.show_share ? 1 : 0};
  d.layout = nova_ui_layout_create(view, &s, MeasureCallback, &d, max_width,
                                   upward ? 1 : 0);
  if (!d.layout) {
    Reset();
    return false;
  }
  d.view = nova_ui_layout_view(d.layout);
  width_ = d.view->width;
  height_ = d.view->height;
  return true;
}

void NovaRenderer::Paint(ID2D1RenderTarget* rt, float scale, int hover) const {
  const Impl& d = *impl_;
  if (!rt || !d.view)
    return;
  const NovaUILayoutView& v = *d.view;
  float old_dx, old_dy;
  rt->GetDpi(&old_dx, &old_dy);
  rt->SetDpi(96.0f, 96.0f);
  D2D1_MATRIX_3X2_F base;
  rt->GetTransform(&base);
  rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * base);

  ComPtr<ID2D1SolidColorBrush> brush;
  rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush);
  auto fill = [&](const NovaUIBox& b, NovaUIColor color) {
    brush->SetColor(ColorOf(color));
    D2D1_RECT_F r = D2D1::RectF((float)b.rect.x, (float)b.rect.y,
                                (float)(b.rect.x + b.rect.width),
                                (float)(b.rect.y + b.rect.height));
    if (b.radius > 0)
      rt->FillRoundedRectangle(D2D1::RoundedRect(r, (float)b.radius,
                                                 (float)b.radius),
                               brush.Get());
    else
      rt->FillRectangle(r, brush.Get());
  };

  if (v.card) {
    NovaUIBox card = {{0, 0, v.width, v.height},
                      nova_ui_corner_radius(),
                      NOVA_COLOR_CARD};
    fill(card, NOVA_COLOR_CARD);
  }
  for (int i = 0; i < v.link_count; i++)
    fill(v.links[i], v.links[i].color);

  if (v.path_count > 0) {
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> geo;
    if (factory && SUCCEEDED(factory->CreatePathGeometry(&geo))) {
      ComPtr<ID2D1GeometrySink> sink;
      if (SUCCEEDED(geo->Open(&sink))) {
        bool open = false;
        for (int i = 0; i < v.path_count; i++) {
          const NovaUIPathSeg& p = v.path[i];
          D2D1_POINT_2F pt = D2D1::Point2F((float)p.x, (float)p.y);
          if (p.kind == NOVA_PATH_MOVE) {
            if (open)
              sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
            open = true;
          } else if (open) {
            if (p.kind == NOVA_PATH_LINE) {
              sink->AddLine(pt);
            } else {
              sink->AddBezier(D2D1::BezierSegment(
                  D2D1::Point2F((float)p.c1x, (float)p.c1y),
                  D2D1::Point2F((float)p.c2x, (float)p.c2y), pt));
            }
          }
        }
        if (open)
          sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        brush->SetColor(ColorOf(NOVA_COLOR_BRANCH));
        rt->DrawGeometry(geo.Get(), brush.Get(), 1.0f);
      }
    }
  }

  const NovaUIHit* hovered =
      (hover >= 0 && hover < v.hit_count) ? &v.hits[hover] : nullptr;
  for (int i = 0; i < v.box_count; i++) {
    const NovaUIBox& b = v.boxes[i];
    NovaUIColor color = b.color;
    if (color == NOVA_COLOR_ALT_FILL && hovered &&
        b.rect.x == hovered->rect.x && b.rect.y == hovered->rect.y &&
        b.rect.width == hovered->rect.width &&
        b.rect.height == hovered->rect.height)
      color = NOVA_COLOR_ALT_HOVER;
    fill(b, color);
  }

  Impl& dm = *const_cast<Impl*>(impl_);
  ComPtr<ID2D1SolidColorBrush> hl_brush;
  rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &hl_brush);
  for (int i = 0; i < v.run_count; i++) {
    const NovaUIRun& r = v.runs[i];
    std::wstring w = ToWide(r.text);
    if (w.empty())
      continue;
    auto tl = dm.TextLayout(w, r.font);
    if (!tl)
      continue;
    brush->SetColor(ColorOf(r.color));
    if (r.hl_start >= 0 && r.hl_end > r.hl_start) {
      hl_brush->SetColor(ColorOf(r.hl_color));
      UINT32 start = (UINT32)r.hl_start;
      UINT32 len = (UINT32)(r.hl_end - r.hl_start);
      if (start < w.size()) {
        len = (std::min<UINT32>)(len, (UINT32)w.size() - start);
        tl->SetDrawingEffect(hl_brush.Get(), DWRITE_TEXT_RANGE{start, len});
      }
    }
    DWRITE_LINE_METRICS lm{};
    UINT32 n = 0;
    tl->GetLineMetrics(&lm, 1, &n);
    rt->DrawTextLayout(D2D1::Point2F((float)r.x, (float)(r.baseline - lm.baseline)),
                       tl.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
  }
  rt->SetTransform(base);
  rt->SetDpi(old_dx, old_dy);
}

int NovaRenderer::HitTest(double x, double y) const {
  if (!impl_->view)
    return -1;
  const NovaUILayoutView& v = *impl_->view;
  // first match wins, same as the Mac panel
  for (int i = 0; i < v.hit_count; i++) {
    const NovaUIRect& r = v.hits[i].rect;
    if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
      return i;
  }
  return -1;
}

bool NovaRenderer::GetHit(int index, NovaUIHit* hit) const {
  if (!impl_->view || index < 0 || index >= impl_->view->hit_count)
    return false;
  *hit = impl_->view->hits[index];
  return true;
}

int NovaRenderer::BlockEnd(int index) const {
  const NovaUIContent* c = impl_->source;
  if (!c || index < 0 || index >= c->block_count)
    return -1;
  return c->blocks[index].to;
}

}  // namespace weasel
