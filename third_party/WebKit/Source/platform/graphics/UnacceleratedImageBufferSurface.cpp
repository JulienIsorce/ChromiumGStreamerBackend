/*
 * Copyright (c) 2013, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "platform/graphics/UnacceleratedImageBufferSurface.h"

#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkDevice.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "wtf/PassRefPtr.h"

namespace blink {

UnacceleratedImageBufferSurface::UnacceleratedImageBufferSurface(const IntSize& size, OpacityMode opacityMode, ImageInitializationMode initializationMode)
    : ImageBufferSurface(size, opacityMode)
{
    SkAlphaType alphaType = (Opaque == opacityMode) ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
    SkImageInfo info = SkImageInfo::MakeN32(size.width(), size.height(), alphaType);
    SkSurfaceProps disableLCDProps(0, kUnknown_SkPixelGeometry);
    m_surface = adoptRef(SkSurface::NewRaster(info, Opaque == opacityMode ? 0 : &disableLCDProps));

    if (initializationMode == InitializeImagePixels) {
        if (m_surface)
            clear();
    }
}

UnacceleratedImageBufferSurface::~UnacceleratedImageBufferSurface() { }

SkCanvas* UnacceleratedImageBufferSurface::canvas()
{
    return m_surface->getCanvas();
}

const SkBitmap& UnacceleratedImageBufferSurface::deprecatedBitmapForOverwrite()
{
    m_surface->notifyContentWillChange(SkSurface::kDiscard_ContentChangeMode);
    return m_surface->getCanvas()->getDevice()->accessBitmap(false);
}

bool UnacceleratedImageBufferSurface::isValid() const
{
    return m_surface;
}

PassRefPtr<SkImage> UnacceleratedImageBufferSurface::newImageSnapshot(AccelerationHint)
{
    return adoptRef(m_surface->newImageSnapshot());
}

} // namespace blink
