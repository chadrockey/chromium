// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/resources/ui_resource_bitmap.h"

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkMallocPixelRef.h"
#include "third_party/skia/include/core/SkPixelRef.h"

namespace cc {

void UIResourceBitmap::Create(const skia::RefPtr<SkPixelRef>& pixel_ref,
                              UIResourceFormat format) {
  const SkImageInfo& info = pixel_ref->info();
  DCHECK(info.fWidth);
  DCHECK(info.fHeight);
  DCHECK(pixel_ref);
  DCHECK(pixel_ref->isImmutable());
  format_ = format;
  size_ = gfx::Size(info.fWidth, info.fHeight);
  pixel_ref_ = pixel_ref;

  // Default values for secondary parameters.
  wrap_mode_ = CLAMP_TO_EDGE;
  opaque_ = (format == ETC1);
}

UIResourceBitmap::UIResourceBitmap(const SkBitmap& skbitmap) {
  DCHECK_EQ(skbitmap.config(), SkBitmap::kARGB_8888_Config);
  DCHECK_EQ(skbitmap.width(), skbitmap.rowBytesAsPixels());
  DCHECK(skbitmap.isImmutable());

  skia::RefPtr<SkPixelRef> pixel_ref = skia::SharePtr(skbitmap.pixelRef());
  Create(pixel_ref, UIResourceBitmap::RGBA8);

  SetOpaque(skbitmap.isOpaque());
}

UIResourceBitmap::UIResourceBitmap(const skia::RefPtr<SkPixelRef>& pixel_ref) {
  Create(pixel_ref, UIResourceBitmap::ETC1);
}

UIResourceBitmap::~UIResourceBitmap() {}

AutoLockUIResourceBitmap::AutoLockUIResourceBitmap(
    const UIResourceBitmap& bitmap) : bitmap_(bitmap) {
  bitmap_.pixel_ref_->lockPixels();
}

AutoLockUIResourceBitmap::~AutoLockUIResourceBitmap() {
  bitmap_.pixel_ref_->unlockPixels();
}

const uint8_t* AutoLockUIResourceBitmap::GetPixels() const {
  return static_cast<const uint8_t*>(bitmap_.pixel_ref_->pixels());
}

}  // namespace cc
