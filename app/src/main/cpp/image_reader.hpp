#ifndef IMAGE_READER_HPP
#define IMAGE_READER_HPP

#include <media/NdkImageReader.h>

#include <functional>

/*
 * ImageFormat:
 *     A Data Structure to communicate resolution between camera and ImageReader
 */
struct ImageFormat {
    int32_t width;
    int32_t height;

    int32_t format;  // Through out this demo, the format is fixed to
                     // YUV_420 format
};

class ImageReader {
  public:
    /**
     * Ctor and Dtor()
     */
    explicit ImageReader(ImageFormat* res, enum AIMAGE_FORMATS format);

    ~ImageReader();

    /**
     * Report cached ANativeWindow, which was used to create camera's capture
     * session output.
     */
    ANativeWindow* GetNativeWindow(void);

    /**
     * Retrieve Image on the top of Reader's queue
     */
    AImage* GetNextImage(void);

    /**
     * Retrieve Image on the back of Reader's queue, dropping older images
     */
    AImage* GetLatestImage(void);

    /**
     * Delete Image
     * @param image {@link AImage} instance to be deleted
     */
    void DeleteImage(AImage* image);

    /**
     * AImageReader callback handler. Called by AImageReader when a frame is
     * captured
     * (Internal function, not to be called by clients)
     */
    void ImageCallback(AImageReader* reader);

    /**
     * DisplayImage()
     *   Present camera image to the given display buffer. Avaliable image is
     * converted
     *   to display buffer format. Supported display format:
     *      WINDOW_FORMAT_RGBX_8888
     *      WINDOW_FORMAT_RGBA_8888
     *   @param buf {@link ANativeWindow_Buffer} for image to display to.
     *   @param image a {@link AImage} instance, source of image conversion.
     *            it will be deleted via {@link AImage_delete}
     *   @return true on success, false on failure
     */
    bool DisplayImage(ANativeWindow_Buffer* buf, AImage* image);
    /**
     * Configure the rotation angle necessary to apply to
     * Camera image when presenting: all rotations should be accumulated:
     *    CameraSensorOrientation + Android Device Native Orientation +
     *    Human Rotation (rotated degree related to Phone native orientation
     */
    void SetPresentRotation(int32_t angle);

    /**
     * regsiter a callback function for client to be notified that jpeg already
     * written out.
     * @param ctx is client context when callback is invoked
     * @param callback is the actual callback function
     */
    void RegisterCallback(
        void* ctx, std::function<void(void* ctx, const char* fileName)>
    );

    [[nodiscard]] AHardwareBuffer* getImageHardwareBuffer() const noexcept;

  private:
    int32_t presentRotation_;
    AImageReader* reader_;

    std::function<void(void* ctx, const char* fileName)> callback_;
    void* callbackCtx_;

    void PresentImage(ANativeWindow_Buffer* buf, AImage* image);
    void PresentImage90(ANativeWindow_Buffer* buf, AImage* image);
    void PresentImage180(ANativeWindow_Buffer* buf, AImage* image);
    void PresentImage270(ANativeWindow_Buffer* buf, AImage* image);

    void WriteFile(AImage* image);
};

#endif  // IMAGE_READER_HPP
