#pragma once
// Nova candidate panel: assembles nova-ui content from a weasel Context,
// lays it out with nova-ui and paints it with Direct2D / DirectWrite.
// Coordinates are logical (96 dpi) units; Paint() applies the dpi scale.
#include <WeaselIPCData.h>
#include <d2d1.h>
#include <dwrite.h>
#include <nova_ui.h>
#include <wrl.h>

#include <memory>
#include <string>
#include <vector>

namespace weasel {

class NovaRenderer {
 public:
  struct Style {
    std::wstring face;      // body font family, may be empty
    double font_size = 20;  // logical px (font point * 96 / 72)
    bool linear = false;    // plain list laid out in one row
    bool show_share = false;
    // style/inline_preedit: the input is written into the document, so the
    // panel does not repeat it (same rule as Squirrel).
    bool inline_preedit = false;
    // Nova theme: colors, fonts, font size and shapes. When set, it wins over
    // face / font_size above (font/size is in points, like on the Mac).
    std::shared_ptr<NovaUITheme> theme;
  };

  NovaRenderer();
  ~NovaRenderer();
  NovaRenderer(const NovaRenderer&) = delete;
  NovaRenderer& operator=(const NovaRenderer&) = delete;

  // Build the layout. Returns false when there is nothing to show.
  bool Build(IDWriteFactory* dwrite,
             const Context& ctx,
             const Style& style,
             double max_width,
             bool upward);
  // Lay out already assembled content (the caller keeps `content` alive while
  // the renderer is used). Build() calls this after nova_ui_assemble.
  bool BuildContent(IDWriteFactory* dwrite,
                    const NovaUIContent* content,
                    const Style& style,
                    double max_width,
                    bool upward);
  void Reset();
  // Logical size of the panel (valid after a successful Build).
  double width() const { return width_; }
  double height() const { return height_; }

  // Paint at the render target's origin. `scale` = dpi / 96.
  void Paint(ID2D1RenderTarget* rt, float scale, int hover) const;

  // Index into hits, or -1. x / y are logical coordinates.
  int HitTest(double x, double y) const;
  bool GetHit(int index, NovaUIHit* hit) const;
  // End (raw letter position) of block `index`, or -1.
  int BlockEnd(int index) const;

  static double FullHeight(const Style& style);
  // Space around the panel content (theme shape/edge).
  static double Edge(const Style& style);

  struct Impl;

 private:
  Impl* impl_;
  double width_ = 0, height_ = 0;
};

}  // namespace weasel
