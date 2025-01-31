/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkCGUtils.h"
#include "SkEncodedOrigin.h"
#include "SkImageGeneratorCG.h"
#include "SkPixmapPriv.h"
#include "SkTemplates.h"

#ifdef SK_BUILD_FOR_MAC
#include <ApplicationServices/ApplicationServices.h>
#endif

#ifdef SK_BUILD_FOR_IOS
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <MobileCoreServices/MobileCoreServices.h>
#endif

namespace {
class ImageGeneratorCG : public SkImageGenerator {
public:
    /* Takes ownership of the imageSrc */
    ImageGeneratorCG(const SkImageInfo&, const void* imageSrc, sk_sp<SkData> data, SkEncodedOrigin);

protected:
    sk_sp<SkData> onRefEncodedData() override;

    bool onGetPixels(const SkImageInfo&, void* pixels, size_t rowBytes, const Options&) override;

private:
    SkAutoTCallVProc<const void, CFRelease> fImageSrc;
    sk_sp<SkData>                           fData;
    const SkEncodedOrigin                   fOrigin;

    typedef SkImageGenerator INHERITED;
};

static CGImageSourceRef data_to_CGImageSrc(SkData* data) {
    CGDataProviderRef cgData = CGDataProviderCreateWithData(data, data->data(), data->size(),
                                                            nullptr);
    if (!cgData) {
        return nullptr;
    }
    CGImageSourceRef imageSrc = CGImageSourceCreateWithDataProvider(cgData, 0);
    CGDataProviderRelease(cgData);
    return imageSrc;
}

}  // namespace

std::unique_ptr<SkImageGenerator> SkImageGeneratorCG::MakeFromEncodedCG(sk_sp<SkData> data) {
    CGImageSourceRef imageSrc = data_to_CGImageSrc(data.get());
    if (!imageSrc) {
        return nullptr;
    }

    // Make sure we call CFRelease to free the imageSrc.  Since CFRelease actually takes
    // a const void*, we must cast the imageSrc to a const void*.
    SkAutoTCallVProc<const void, CFRelease> autoImageSrc(imageSrc);

    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(imageSrc, 0, nullptr);
    if (!properties) {
        return nullptr;
    }

    CFNumberRef widthRef = (CFNumberRef) (CFDictionaryGetValue(properties,
            kCGImagePropertyPixelWidth));
    CFNumberRef heightRef = (CFNumberRef) (CFDictionaryGetValue(properties,
            kCGImagePropertyPixelHeight));
    if (nullptr == widthRef || nullptr == heightRef) {
        return nullptr;
    }

    int width, height;
    if (!CFNumberGetValue(widthRef, kCFNumberIntType, &width) ||
            !CFNumberGetValue(heightRef, kCFNumberIntType, &height)) {
        return nullptr;
    }

    bool hasAlpha = (bool) (CFDictionaryGetValue(properties,
            kCGImagePropertyHasAlpha));
    SkAlphaType alphaType = hasAlpha ? kPremul_SkAlphaType : kOpaque_SkAlphaType;
    SkImageInfo info = SkImageInfo::MakeS32(width, height, alphaType);

    auto origin = kDefault_SkEncodedOrigin;
    auto orientationRef = (CFNumberRef) (CFDictionaryGetValue(properties,
            kCGImagePropertyOrientation));
    int originInt;
    if (orientationRef && CFNumberGetValue(orientationRef, kCFNumberIntType, &originInt)) {
        origin = (SkEncodedOrigin) originInt;
    }

    if (SkPixmapPriv::ShouldSwapWidthHeight(origin)) {
        info = SkPixmapPriv::SwapWidthHeight(info);
    }

    // FIXME: We have the opportunity to extract color space information here,
    //        though I think it makes sense to wait until we understand how
    //        we want to communicate it to the generator.

    return std::unique_ptr<SkImageGenerator>(new ImageGeneratorCG(info, autoImageSrc.release(),
                                                                  std::move(data), origin));
}

ImageGeneratorCG::ImageGeneratorCG(const SkImageInfo& info, const void* imageSrc,
                                   sk_sp<SkData> data, SkEncodedOrigin origin)
    : INHERITED(info)
    , fImageSrc(imageSrc)
    , fData(std::move(data))
    , fOrigin(origin)
{}

sk_sp<SkData> ImageGeneratorCG::onRefEncodedData() {
    return fData;
}

bool ImageGeneratorCG::onGetPixels(const SkImageInfo& info, void* pixels, size_t rowBytes,
                                   const Options&)
{
    if (kN32_SkColorType != info.colorType()) {
        // FIXME: Support other colorTypes.
        return false;
    }

    switch (info.alphaType()) {
        case kOpaque_SkAlphaType:
            if (kOpaque_SkAlphaType != this->getInfo().alphaType()) {
                return false;
            }
            break;
        case kPremul_SkAlphaType:
            break;
        default:
            return false;
    }

    CGImageRef image = CGImageSourceCreateImageAtIndex((CGImageSourceRef) fImageSrc.get(), 0,
                                                       nullptr);
    if (!image) {
        return false;
    }
    SkAutoTCallVProc<CGImage, CGImageRelease> autoImage(image);

    SkPixmap dst(info, pixels, rowBytes);
    auto decode = [&image](const SkPixmap& pm) {
        // FIXME: Using SkCopyPixelsFromCGImage (as opposed to swizzling
        // ourselves) greatly restricts the color and alpha types that we
        // support.  If we swizzle ourselves, we can add support for:
        //     kUnpremul_SkAlphaType
        //     16-bit per component RGBA
        //     kGray_8_SkColorType
        // Additionally, it would be interesting to compare the performance
        // of SkSwizzler with CG's built in swizzler.
        return SkCopyPixelsFromCGImage(pm, image);
    };
    return SkPixmapPriv::Orient(dst, fOrigin, decode);
}
