// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_SNAPSHOTTER_H_
#define SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_SNAPSHOTTER_H_

#include <lib/fit/function.h>

#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/scenic/lib/gfx/resources/resource_visitor.h"
#include "src/ui/scenic/lib/gfx/snapshot/serializer.h"
#include "src/ui/scenic/lib/scenic/scenic.h"

namespace scenic_impl {
namespace gfx {

class Node;
class Resource;

// Callback for the snapshotter, where the buffer stores the snapshot data and
// the bool represents the success or lack thereof of the snapshot operation.
using TakeSnapshotCallback = fit::function<void(fuchsia::mem::Buffer, bool)>;

// Defines a |ResourceVisitor| that takes snapshot of a branch of the scene
// graph. It provides the snapshot in flatbuffer formatted |fuchsia.mem.Buffer|.
// It uses |Serializer| set of classes to recreate the node hierarchy while
// visiting every entity of the scenic node. After the visit, the serializer
// generates the flatbuffer in |TakeSnapshot|.
class Snapshotter : public ResourceVisitor {
 public:
  Snapshotter(std::unique_ptr<escher::BatchGpuUploader> gpu_uploader);
  virtual ~Snapshotter() = default;

  // Takes the snapshot of the |node| and calls the |callback| with a
  // |fuchsia.mem.Buffer| buffer.
  void TakeSnapshot(Resource* resource, TakeSnapshotCallback snapshot_callback);

 protected:
  void Visit(Memory* r) override;
  void Visit(Image* r) override;
  void Visit(ImagePipeBase* r) override;
  void Visit(Buffer* r) override;
  void Visit(View* r) override;
  void Visit(ViewNode* r) override;
  void Visit(ViewHolder* r) override;
  void Visit(EntityNode* r) override;
  void Visit(OpacityNode* r) override;
  void Visit(ShapeNode* r) override;
  void Visit(Scene* r) override;
  void Visit(CircleShape* r) override;
  void Visit(RectangleShape* r) override;
  void Visit(RoundedRectangleShape* r) override;
  void Visit(MeshShape* r) override;
  void Visit(Material* r) override;
  void Visit(Compositor* r) override;
  void Visit(DisplayCompositor* r) override;
  void Visit(LayerStack* r) override;
  void Visit(Layer* r) override;
  void Visit(Camera* r) override;
  void Visit(Renderer* r) override;
  void Visit(Light* r) override;
  void Visit(AmbientLight* r) override;
  void Visit(DirectionalLight* r) override;
  void Visit(PointLight* r) override;
  void Visit(Import* r) override;

 private:
  void VisitNode(Node* r);
  void VisitResource(Resource* r);
  void VisitMesh(escher::MeshPtr mesh);
  void VisitImage(escher::ImagePtr i);

  void ReadImage(escher::ImagePtr image, fit::function<void(escher::BufferPtr buffer)> callback);
  void ReadBuffer(escher::BufferPtr buffer, fit::function<void(escher::BufferPtr buffer)> callback);

  std::unique_ptr<escher::BatchGpuUploader> gpu_uploader_;

  // Holds the current serializer for the scenic node being serialized. This is
  // needed when visiting node's content like mesh, material and images.
  std::shared_ptr<NodeSerializer> current_node_serializer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Snapshotter);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_SNAPSHOT_SNAPSHOTTER_H_
