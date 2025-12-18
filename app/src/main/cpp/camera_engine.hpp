#ifndef CAMERA_ENGINE_HPP
#define CAMERA_ENGINE_HPP

#include <android/native_window.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "camera_manager.hpp"

/**
 * basic CameraAppEngine
 */
class CameraEngine {
  public:
    explicit CameraEngine(android_app* app);
    ~CameraEngine();

    // Interfaces to android application framework
    struct android_app* AndroidApp(void) const;
    void OnAppInitWindow(void);
    void DrawFrame(void);
    AHardwareBuffer* getNextHwBuffer();
    AImage* getNextImage();
    void OnAppConfigChange(void);
    void OnAppTermWindow(void);

    // Native Window handlers
    int32_t GetSavedNativeWinWidth(void);
    int32_t GetSavedNativeWinHeight(void);
    int32_t GetSavedNativeWinFormat(void);
    void SaveNativeWinRes(int32_t w, int32_t h, int32_t format);

    // UI handlers
    //    void RequestCameraPermission();
    //    void OnCameraPermission(jboolean granted);
    //    void EnableUI(void);
    //    void OnTakePhoto(void);
    //    void OnCameraParameterChanged(int32_t code, int64_t val);

    // Manage NDKCamera Object
    void CreateCamera(void);
    void DeleteCamera(void);

  private:
    //    void OnPhotoTaken(const char* fileName);
    int GetDisplayRotation();

    struct android_app* app_;
    ImageFormat savedNativeWinRes_;
    bool cameraGranted_;
    int rotation_;
    volatile bool cameraReady_;
    NDKCamera* camera_;
    ImageReader* yuvReader_;
    ImageReader* jpgReader_;
};

/**
 * retrieve global singleton CameraEngine instance
 * @return the only instance of CameraEngine in the app
 */
CameraEngine* GetAppEngine(void);

#endif  // CAMERA_ENGINE_HPP
