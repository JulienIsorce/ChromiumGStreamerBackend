// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/gbm_buffer_base.h"

#include <gbm.h>

#include "base/logging.h"
#include "ui/ozone/platform/drm/gpu/drm_device.h"

namespace ui {

namespace {

// Pixel configuration for the current buffer format.
// TODO(dnicoara) These will need to change once we query the hardware for
// supported configurations.
const uint8_t kColorDepth = 24;
const uint8_t kPixelDepth = 32;

}  // namespace

GbmBufferBase::GbmBufferBase(const scoped_refptr<DrmDevice>& drm,
                             gbm_bo* bo,
                             bool scanout)
    : drm_(drm), bo_(bo) {
  if (scanout) {
    if (!drm_->AddFramebuffer(gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                              kColorDepth, kPixelDepth, gbm_bo_get_stride(bo),
                              gbm_bo_get_handle(bo).u32, &framebuffer_)) {
      PLOG(ERROR) << "Failed to register buffer";
      return;
    }

    fb_pixel_format_ = gbm_bo_get_format(bo);
    if (fb_pixel_format_ == GBM_FORMAT_ARGB8888)
      fb_pixel_format_ = GBM_FORMAT_XRGB8888;

    // For now, we always create a frame buffer of 24 bit color depth and
    // support only XRGB format.
    DCHECK(fb_pixel_format_ == GBM_FORMAT_XRGB8888);
  }
}

GbmBufferBase::~GbmBufferBase() {
  if (framebuffer_)
    drm_->RemoveFramebuffer(framebuffer_);
}

uint32_t GbmBufferBase::GetFramebufferId() const {
  return framebuffer_;
}

uint32_t GbmBufferBase::GetHandle() const {
  return gbm_bo_get_handle(bo_).u32;
}

gfx::Size GbmBufferBase::GetSize() const {
  return gfx::Size(gbm_bo_get_width(bo_), gbm_bo_get_height(bo_));
}

uint32_t GbmBufferBase::GetFramebufferPixelFormat() const {
  return fb_pixel_format_;
}

bool GbmBufferBase::RequiresGlFinish() const {
  return !drm_->is_primary_device();
}

}  // namespace ui
