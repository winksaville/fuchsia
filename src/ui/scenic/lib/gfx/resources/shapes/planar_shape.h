// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_PLANAR_SHAPE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_PLANAR_SHAPE_H_

#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"

namespace scenic_impl {
namespace gfx {

// A shape that lies within the Z=0 plane of the local coordinate system.
// As a result, |GetIntersection()| is implemented by intersecting a ray
// with this plane and calling |ContainsPoint()| on the result.
class PlanarShape : public Shape {
 public:
  // |Shape|
  bool GetIntersection(const escher::ray4& ray, float* out_distance) const override;

  // Returns if the given point lies within its bounds of this shape.
  virtual bool ContainsPoint(const escher::vec2& point) const = 0;

 protected:
  PlanarShape(Session* session, SessionId session_id, ResourceId id,
              const ResourceTypeInfo& type_info);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_SHAPES_PLANAR_SHAPE_H_
