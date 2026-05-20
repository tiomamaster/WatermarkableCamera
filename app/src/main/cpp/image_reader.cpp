#include "image_reader.hpp"

#include "util.hpp"
#include "vulkan_renderer.hpp"

using namespace camera::util;

namespace camera {

void onImageAvailable(void* ctx, AImageReader* reader) {
    reinterpret_cast<ImageReader*>(ctx)->imageCallback(reader);
}

ImageReader::ImageReader(
    int32_t width, int32_t height, AIMAGE_FORMATS format, VkRenderer& vkRenderer
)
    : reader_(nullptr), vkRenderer_(vkRenderer) {
    media_status_t status = AImageReader_newWithUsage(
        width,
        height,
        format,
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
        MAX_BUF_COUNT,
        &reader_
    );
    logAssert(reader_ && status == AMEDIA_OK, "failed to create ImageReader");

    AImageReader_ImageListener listener{
        .context = this, .onImageAvailable = onImageAvailable
    };
    AImageReader_setImageListener(reader_, &listener);
}

ImageReader::~ImageReader() {
    logAssert(reader_, "reader_ is null");
    AImageReader_setImageListener(reader_, NULL);
    AImageReader_delete(reader_);
}

void ImageReader::imageCallback(AImageReader*) {
    int32_t format;
    media_status_t status = AImageReader_getFormat(reader_, &format);
    logAssert(status == AMEDIA_OK, "failed to get the media format");
    auto image = getNextImage();
    if (!image) return;

    AHardwareBuffer* hwBuffer;
    status = AImage_getHardwareBuffer(image, &hwBuffer);

    if (status != AMEDIA_OK) {
        logE("Can't acquire hw buffer");
        AImage_delete(image);
        return;
    }

    AHardwareBuffer_acquire(hwBuffer);

    if (format == AIMAGE_FORMAT_YUV_420_888) {
        vkRenderer_.camHwBufferToTexture(hwBuffer);
    } else if (format == AIMAGE_FORMAT_RGBA_8888) {
        vkRenderer_.watHwBufferToTexture(hwBuffer);
    } else {
        logI("Unknown format");
    }

    AHardwareBuffer_release(hwBuffer);
    AImage_delete(image);
}

ANativeWindow* ImageReader::getNativeWindow() {
    logAssert(reader_, "reader_ is null");
    ANativeWindow* nativeWindow;
    media_status_t status = AImageReader_getWindow(reader_, &nativeWindow);
    logAssert(status == AMEDIA_OK, "could not get ANativeWindow");
    return nativeWindow;
}

AImage* ImageReader::getNextImage() {
    AImage* image;
    media_status_t status = AImageReader_acquireNextImage(reader_, &image);
    if (status != AMEDIA_OK) return nullptr;
    return image;
}

AImage* ImageReader::getLatestImage() {
    AImage* image;
    media_status_t status = AImageReader_acquireLatestImage(reader_, &image);
    if (status != AMEDIA_OK) return nullptr;
    return image;
}

void ImageReader::deleteImage(AImage* image) {
    if (image) AImage_delete(image);
}

}  // namespace camera
