// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_TESTING_VIEWS_COLOR_H_
#define GARNET_TESTING_VIEWS_COLOR_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <map>
#include <ostream>
#include <tuple>

namespace scenic {

struct Color {
  // Constructor is idiomatic RGBA, but memory layout is native BGRA.
  constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : b(b), g(g), r(r), a(a) {}

  uint8_t b;
  uint8_t g;
  uint8_t r;
  uint8_t a;
};

inline bool operator==(const Color& a, const Color& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline bool operator<(const Color& a, const Color& b) {
  return std::tie(a.r, a.g, a.b, a.a) < std::tie(b.r, b.g, b.b, b.a);
}

// RGBA hex dump. Note that this differs from the internal BGRA memory layout.
std::ostream& operator<<(std::ostream& os, const Color& c);

class Screenshot {
 public:
  Screenshot(const fuchsia::ui::scenic::ScreenshotData& screenshot_data);

  size_t width() const { return width_; }
  size_t height() const { return height_; }
  bool empty() const { return width_ == 0 || height_ == 0; }

  // Notably the indexer behaves like a row-major matrix, whereas the iterator
  // behaves like a flat array.
  const Color* operator[](size_t row) const;

  // Coordinates are in the range [0, 1).
  const Color& ColorAt(float x, float y) const;

  // Notably the iterator behaves like a flat array, whereas the indexer behaves
  // like a row-major matrix.
  const Color* begin() const;
  const Color* end() const;

  // Counts the frequencies of each color in a screenshot.
  std::map<Color, size_t> Histogram() const;

 private:
  const size_t width_, height_;
  std::vector<uint8_t> data_;
};

}  // namespace scenic

#endif  // GARNET_TESTING_VIEWS_COLOR_H_
