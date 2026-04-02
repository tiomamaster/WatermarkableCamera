#pragma once

#include <media/NdkImageReader.h>

#include <cstdint>
#include <functional>

// TODO: remove
struct ImageFormat {
    int32_t width;
    int32_t height;

    int32_t format;
};

namespace camera {

class ImageReader {
  public:
    ImageReader(int32_t width, int32_t height, AIMAGE_FORMATS format);
    ~ImageReader();

    friend void onImageAvailable(void* ctx, AImageReader* reader);
    ANativeWindow* getNativeWindow();

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

    AImageReader* reader_;

    void imageCallback(AImageReader* reader);
};

}  // namespace camera
