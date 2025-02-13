// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/media_session/public/cpp/media_session_mojom_traits.h"

#include "mojo/public/cpp/base/string16_mojom_traits.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/mojo/geometry_struct_traits.h"
#include "url/mojom/url_gurl_mojom_traits.h"

namespace mojo {

// static
bool StructTraits<media_session::mojom::MediaImageDataView,
                  media_session::MediaImage>::
    Read(media_session::mojom::MediaImageDataView data,
         media_session::MediaImage* out) {
  if (!data.ReadSrc(&out->src))
    return false;
  if (!data.ReadType(&out->type))
    return false;
  if (!data.ReadSizes(&out->sizes))
    return false;

  return true;
}

// static
bool StructTraits<media_session::mojom::MediaMetadataDataView,
                  media_session::MediaMetadata>::
    Read(media_session::mojom::MediaMetadataDataView data,
         media_session::MediaMetadata* out) {
  if (!data.ReadTitle(&out->title))
    return false;

  if (!data.ReadArtist(&out->artist))
    return false;

  if (!data.ReadAlbum(&out->album))
    return false;

  if (!data.ReadSourceTitle(&out->source_title))
    return false;

  return true;
}

// static
const base::span<const uint8_t>
StructTraits<media_session::mojom::MediaImageBitmapDataView,
             SkBitmap>::pixel_data(const SkBitmap& r) {
  const SkImageInfo& info = r.info();
  DCHECK_EQ(info.colorType(), kRGBA_8888_SkColorType);

  return base::make_span(static_cast<uint8_t*>(r.getPixels()),
                         r.computeByteSize());
}

// static
bool StructTraits<media_session::mojom::MediaImageBitmapDataView, SkBitmap>::
    Read(media_session::mojom::MediaImageBitmapDataView data, SkBitmap* out) {
  mojo::ArrayDataView<uint8_t> pixel_data;
  data.GetPixelDataDataView(&pixel_data);

  SkImageInfo info = SkImageInfo::Make(
      data.width(), data.height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  if (info.computeByteSize(info.minRowBytes()) > pixel_data.size()) {
    // Insufficient buffer size.
    return false;
  }

  // Create the SkBitmap object which wraps the arc bitmap pixels. This
  // doesn't copy and |data| and |bitmap| share the buffer.
  SkBitmap bitmap;
  if (!bitmap.installPixels(info, const_cast<uint8_t*>(pixel_data.data()),
                            info.minRowBytes())) {
    // Error in installing pixels.
    return false;
  }

  // Copy the pixels with converting color type.
  SkImageInfo image_info = info.makeColorType(kN32_SkColorType);
  return out->tryAllocPixels(image_info) &&
         bitmap.readPixels(image_info, out->getPixels(), out->rowBytes(), 0, 0);
}

}  // namespace mojo
