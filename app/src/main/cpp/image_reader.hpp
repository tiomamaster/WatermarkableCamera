#pragma once

#include <media/NdkImageReader.h>

#include <cstdint>

#include "vulkan_renderer.hpp"

namespace camera {

class ImageReader {
  public:
    ImageReader() = default;
    ImageReader(
        int32_t width,
        int32_t height,
        AIMAGE_FORMATS format,
        VkRenderer* vkRenderer
    );
    ImageReader(ImageReader&& other) noexcept;
    ImageReader& operator=(ImageReader&& other) noexcept;
    ~ImageReader();

    friend void swap(ImageReader& first, ImageReader& second) noexcept;
    friend void onImageAvailable(void* ctx, AImageReader* reader);
    ANativeWindow* getNativeWindow();
    void removeImageCallback();

    /**
     * Acquire the next image from the image reader's queue.
     */
    AImage* getNextImage();

    /**
     * Acquire the latest image from the image reader's queue, dropping
     * older images.
     */
    AImage* getLatestImage();

    void deleteImage(AImage* image);

  private:
    static constexpr const char* DIR_NAME = "/sdcard/DCIM/Camera/";
    static constexpr const char* FILE_NAME = "capture";
    static constexpr int32_t MAX_BUF_COUNT = 2;

    AImageReader* reader_ = nullptr;
    VkRenderer* vkRenderer_ = nullptr;

    void setImageCallback();
    void imageCallback(AImageReader* reader);
};

}  // namespace camera
