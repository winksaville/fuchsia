// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_IMAGE_FORMAT_LLCPP_IMAGE_FORMAT_LLCPP_H_
#define LIB_IMAGE_FORMAT_LLCPP_IMAGE_FORMAT_LLCPP_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/image-format/image_format.h>

namespace image_format {

fuchsia_sysmem_ImageFormatConstraints GetCConstraints(
    const llcpp::fuchsia::sysmem::ImageFormatConstraints& cpp);

llcpp::fuchsia::sysmem::PixelFormat GetCppPixelFormat(
    const fuchsia_sysmem_PixelFormat& pixel_format);

constexpr llcpp::fuchsia::sysmem::ImageFormatConstraints GetDefaultImageFormatConstraints() {
  llcpp::fuchsia::sysmem::ImageFormatConstraints constraints;
  // Should match values in constraints.fidl.
  // TODO(fxb/35314): LLCPP should initialize to default values.
  constraints.max_coded_width_times_coded_height = 0xffffffff;
  constraints.layers = 1;
  constraints.coded_width_divisor = 1;
  constraints.coded_height_divisor = 1;
  constraints.bytes_per_row_divisor = 1;
  constraints.start_offset_divisor = 1;
  constraints.display_width_divisor = 1;
  constraints.display_height_divisor = 1;

  return constraints;
}

constexpr llcpp::fuchsia::sysmem::BufferMemoryConstraints GetDefaultBufferMemoryConstraints() {
  // Should match values in constraints.fidl.
  // TODO(fxb/35314): LLCPP should initialize to default values.
  return llcpp::fuchsia::sysmem::BufferMemoryConstraints{.min_size_bytes = 0,
                                                         .max_size_bytes = 0xffffffff,
                                                         .physically_contiguous_required = false,
                                                         .secure_required = false,
                                                         .ram_domain_supported = false,
                                                         .cpu_domain_supported = true,
                                                         .inaccessible_domain_supported = false,
                                                         .heap_permitted_count = 0};
}

bool GetMinimumRowBytes(const llcpp::fuchsia::sysmem::ImageFormatConstraints& constraints,
                        uint32_t width, uint32_t* bytes_per_row_out);

}  // namespace image_format

#endif  // LIB_IMAGE_FORMAT_LLCPP_IMAGE_FORMAT_LLCPP_H_
