// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/hardware_display_plane_manager.h"

#include <set>

#include "base/logging.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/platform/drm/gpu/drm_device.h"
#include "ui/ozone/platform/drm/gpu/scanout_buffer.h"

namespace ui {
namespace {

const float kFixedPointScaleValue = 65536.0f;

}  // namespace

HardwareDisplayPlaneList::HardwareDisplayPlaneList() {
#if defined(USE_DRM_ATOMIC)
  atomic_property_set.reset(drmModeAtomicAlloc());
#endif  // defined(USE_DRM_ATOMIC)
}

HardwareDisplayPlaneList::~HardwareDisplayPlaneList() {
}

HardwareDisplayPlaneList::PageFlipInfo::PageFlipInfo(uint32_t crtc_id,
                                                     uint32_t framebuffer,
                                                     CrtcController* crtc)
    : crtc_id(crtc_id), framebuffer(framebuffer), crtc(crtc) {
}

HardwareDisplayPlaneList::PageFlipInfo::~PageFlipInfo() {
}

HardwareDisplayPlaneList::PageFlipInfo::Plane::Plane(int plane,
                                                     int framebuffer,
                                                     const gfx::Rect& bounds,
                                                     const gfx::Rect& src_rect)
    : plane(plane),
      framebuffer(framebuffer),
      bounds(bounds),
      src_rect(src_rect) {
}

HardwareDisplayPlaneList::PageFlipInfo::Plane::~Plane() {
}

HardwareDisplayPlaneManager::HardwareDisplayPlaneManager() : drm_(nullptr) {
}

HardwareDisplayPlaneManager::~HardwareDisplayPlaneManager() {
}

bool HardwareDisplayPlaneManager::Initialize(DrmDevice* drm) {
  drm_ = drm;

  // Try to get all of the planes if possible, so we don't have to try to
  // discover hidden primary planes.
  bool has_universal_planes = false;
#if defined(DRM_CLIENT_CAP_UNIVERSAL_PLANES)
  has_universal_planes = drm->SetCapability(DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
#endif  // defined(DRM_CLIENT_CAP_UNIVERSAL_PLANES)

  ScopedDrmResourcesPtr resources(drmModeGetResources(drm->get_fd()));
  if (!resources) {
    PLOG(ERROR) << "Failed to get resources";
    return false;
  }

  ScopedDrmPlaneResPtr plane_resources(drmModeGetPlaneResources(drm->get_fd()));
  if (!plane_resources) {
    PLOG(ERROR) << "Failed to get plane resources";
    return false;
  }

  crtcs_.clear();
  for (int i = 0; i < resources->count_crtcs; ++i) {
    crtcs_.push_back(resources->crtcs[i]);
  }

  uint32_t num_planes = plane_resources->count_planes;
  std::set<uint32_t> plane_ids;
  for (uint32_t i = 0; i < num_planes; ++i) {
    ScopedDrmPlanePtr drm_plane(
        drmModeGetPlane(drm->get_fd(), plane_resources->planes[i]));
    if (!drm_plane) {
      PLOG(ERROR) << "Failed to get plane " << i;
      return false;
    }

    uint32_t formats_size = drm_plane->count_formats;
    plane_ids.insert(drm_plane->plane_id);
    scoped_ptr<HardwareDisplayPlane> plane(
        CreatePlane(drm_plane->plane_id, drm_plane->possible_crtcs));

    std::vector<uint32_t> supported_formats(formats_size);
    for (uint32_t j = 0; j < formats_size; j++)
      supported_formats[j] = drm_plane->formats[j];

    if (plane->Initialize(drm, supported_formats, false, false)) {
      // CRTC controllers always assume they have a cursor plane and the cursor
      // plane is updated via cursor specific DRM API. Hence, we dont keep
      // track of Cursor plane here to avoid re-using it for any other purpose.
      if (plane->type() != HardwareDisplayPlane::kCursor)
        planes_.push_back(plane.Pass());
    }
  }

  // crbug.com/464085: if driver reports no primary planes for a crtc, create a
  // dummy plane for which we can assign exactly one overlay.
  // TODO(dnicoara): refactor this to simplify AssignOverlayPlanes and move
  // this workaround into HardwareDisplayPlaneLegacy.
  if (!has_universal_planes) {
    for (int i = 0; i < resources->count_crtcs; ++i) {
      if (plane_ids.find(resources->crtcs[i] - 1) == plane_ids.end()) {
        scoped_ptr<HardwareDisplayPlane> dummy_plane(
            CreatePlane(resources->crtcs[i] - 1, (1 << i)));
        if (dummy_plane->Initialize(drm, std::vector<uint32_t>(), true,
                                    false)) {
          planes_.push_back(dummy_plane.Pass());
        }
      }
    }
  }

  std::sort(planes_.begin(), planes_.end(),
            [](HardwareDisplayPlane* l, HardwareDisplayPlane* r) {
              return l->plane_id() < r->plane_id();
            });
  return true;
}

scoped_ptr<HardwareDisplayPlane> HardwareDisplayPlaneManager::CreatePlane(
    uint32_t plane_id,
    uint32_t possible_crtcs) {
  return scoped_ptr<HardwareDisplayPlane>(
      new HardwareDisplayPlane(plane_id, possible_crtcs));
}

HardwareDisplayPlane* HardwareDisplayPlaneManager::FindNextUnusedPlane(
    size_t* index,
    uint32_t crtc_index,
    const OverlayPlane& overlay) const {
  for (size_t i = *index; i < planes_.size(); ++i) {
    auto plane = planes_[i];
    if (!plane->in_use() && IsCompatible(plane, overlay, crtc_index)) {
      *index = i + 1;
      return plane;
    }
  }
  return nullptr;
}

int HardwareDisplayPlaneManager::LookupCrtcIndex(uint32_t crtc_id) const {
  for (size_t i = 0; i < crtcs_.size(); ++i)
    if (crtcs_[i] == crtc_id)
      return i;
  return -1;
}

bool HardwareDisplayPlaneManager::IsCompatible(HardwareDisplayPlane* plane,
                                               const OverlayPlane& overlay,
                                               uint32_t crtc_index) const {
  if (!plane->CanUseForCrtc(crtc_index))
    return false;

  if (!plane->IsSupportedFormat(overlay.buffer->GetFramebufferPixelFormat()))
    return false;

  // TODO(kalyank): We should check for z-order and any needed transformation
  // support. Driver doesn't expose any property to check for z-order, can we
  // rely on the sorting we do based on plane ids ?

  return true;
}

void HardwareDisplayPlaneManager::BeginFrame(
    HardwareDisplayPlaneList* plane_list) {
  for (auto* plane : plane_list->old_plane_list) {
    plane->set_in_use(false);
  }
}

bool HardwareDisplayPlaneManager::AssignOverlayPlanes(
    HardwareDisplayPlaneList* plane_list,
    const OverlayPlaneList& overlay_list,
    uint32_t crtc_id,
    CrtcController* crtc) {
  int crtc_index = LookupCrtcIndex(crtc_id);
  if (crtc_index < 0) {
    LOG(ERROR) << "Cannot find crtc " << crtc_id;
    return false;
  }

  size_t plane_idx = 0;
  for (const auto& plane : overlay_list) {
    HardwareDisplayPlane* hw_plane =
        FindNextUnusedPlane(&plane_idx, crtc_index, plane);
    if (!hw_plane) {
      LOG(ERROR) << "Failed to find a free plane for crtc " << crtc_id;
      return false;
    }

    gfx::Rect fixed_point_rect;
    if (hw_plane->type() != HardwareDisplayPlane::kDummy) {
      const gfx::Size& size = plane.buffer->GetSize();
      gfx::RectF crop_rect = gfx::RectF(plane.crop_rect);
      crop_rect.Scale(size.width(), size.height());

      // This returns a number in 16.16 fixed point, required by the DRM overlay
      // APIs.
      auto to_fixed_point =
          [](double v) -> uint32_t { return v * kFixedPointScaleValue; };
      fixed_point_rect = gfx::Rect(to_fixed_point(crop_rect.x()),
                                   to_fixed_point(crop_rect.y()),
                                   to_fixed_point(crop_rect.width()),
                                   to_fixed_point(crop_rect.height()));
    }

    plane_list->plane_list.push_back(hw_plane);
    hw_plane->set_owning_crtc(crtc_id);
    if (SetPlaneData(plane_list, hw_plane, plane, crtc_id, fixed_point_rect,
                     crtc)) {
      hw_plane->set_in_use(true);
    } else {
      return false;
    }
  }
  return true;
}

std::vector<uint32_t>
HardwareDisplayPlaneManager::GetCompatibleHardwarePlaneIds(
    const OverlayPlane& plane,
    uint32_t crtc_id) const {
  int crtc_index = LookupCrtcIndex(crtc_id);
  if (crtc_index < 0) {
    LOG(ERROR) << "Cannot find crtc " << crtc_id;
    return std::vector<uint32_t>();
  }

  std::vector<uint32_t> plane_ids;
  for (auto* hardware_plane : planes_) {
    if (IsCompatible(hardware_plane, plane, crtc_index))
      plane_ids.push_back(hardware_plane->plane_id());
  }

  return plane_ids;
}

}  // namespace ui
