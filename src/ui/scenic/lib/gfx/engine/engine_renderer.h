// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_ENGINE_RENDERER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_ENGINE_RENDERER_H_

#include <lib/zx/time.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_renderer_config.h"

namespace scenic_impl {
namespace gfx {

class Layer;
class Camera;

// EngineRenderer knows how to render Scenic layers using escher::PaperRenderer.
class EngineRenderer {
 public:
  explicit EngineRenderer(escher::EscherWeakPtr weak_escher, vk::Format depth_stencil_format);
  ~EngineRenderer();

  // Use GPU to render all layers into separate images, and compose them all
  // into |output_image|.
  void RenderLayers(const escher::FramePtr& frame, zx::time target_presentation_time,
                    const escher::ImagePtr& output_image, const std::vector<Layer*>& layers);

 private:
  void DrawLayer(const escher::FramePtr& frame, zx::time target_presentation_time, Layer* layer,
                 const escher::ImagePtr& output_image, const escher::Model& overlay_model);

  void DrawLayerWithPaperRenderer(const escher::FramePtr& frame, zx::time target_presentation_time,
                                  Layer* layer, escher::PaperRendererShadowType shadow_type,
                                  const escher::ImagePtr& output_image,
                                  const escher::Model& overlay_model);

  escher::ImagePtr GetLayerFramebufferImage(uint32_t width, uint32_t height,
                                            bool use_protected_memory);

  std::vector<escher::Camera> GenerateEscherCamerasForPaperRenderer(
      const escher::FramePtr& frame, Camera* camera, escher::ViewingVolume viewing_volume,
      zx::time target_presentation_time);

  escher::MaterialPtr GetReplacementMaterial();

  const escher::EscherWeakPtr escher_;
  escher::PaperRendererPtr paper_renderer_;
  std::unique_ptr<escher::hmd::PoseBufferLatchingShader> pose_buffer_latching_shader_;
  vk::Format depth_stencil_format_ = vk::Format::eUndefined;
  escher::MaterialPtr replacement_material_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_ENGINE_RENDERER_H_
