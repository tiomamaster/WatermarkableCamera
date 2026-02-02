#include "camera_engine.hpp"

#include "native_debug.hpp"

/**
 * constructor and destructor for main application class
 * @param app native_app_glue environment
 * @return none
 */
CameraEngine::CameraEngine(android_app* app)
    : app_(app),
      cameraGranted_(true),
      rotation_(0),
      cameraReady_(false),
      camera_(nullptr),
      yuvReader_(nullptr),
      watReader_(nullptr),
      jpgReader_(nullptr) {
    memset(&savedNativeWinRes_, 0, sizeof(savedNativeWinRes_));
}

CameraEngine::~CameraEngine() {
    cameraReady_ = false;
    DeleteCamera();
}

struct android_app* CameraEngine::AndroidApp(void) const { return app_; }

/**
 * Create a camera object for onboard BACK_FACING camera
 */
void CameraEngine::CreateCamera(void) {
    // Camera needed to be requested at the run-time from Java SDK
    // if Not granted, do nothing.
    if (!cameraGranted_ || !app_->window) {
        LOGW("Camera Sample requires Full Camera access");
        return;
    }

    int32_t displayRotation = GetDisplayRotation();
    rotation_ = displayRotation;

    camera_ = new NDKCamera();
    ASSERT(camera_, "Failed to Create CameraObject");

    int32_t facing = 0, angle = 0, imageRotation = 0;
    if (camera_->GetSensorOrientation(&facing, &angle)) {
        if (facing == ACAMERA_LENS_FACING_FRONT) {
            imageRotation = (angle + rotation_) % 360;
            imageRotation = (360 - imageRotation) % 360;
        } else {
            imageRotation = (angle - rotation_ + 360) % 360;
        }
    }
    LOGI(
        "Phone Rotation: %d, Present Rotation Angle: %d",
        rotation_,
        imageRotation
    );
    ImageFormat view{0, 0, 0}, capture{0, 0, 0}, wat{1080, 2400, 0};
    camera_->MatchCaptureSizeRequest(app_->window, &view, &capture);

    ASSERT(view.width && view.height, "Could not find supportable resolution");

    LOGI("Selected camera preview w = %i, h = %i", view.width, view.height);

    // Request the necessary nativeWindow to OS
    bool portraitNativeWindow =
        (savedNativeWinRes_.width < savedNativeWinRes_.height);
    //    ANativeWindow_setBuffersGeometry(
    //        app_->window, portraitNativeWindow ? view.height : view.width,
    //        portraitNativeWindow ? view.width : view.height,
    //        WINDOW_FORMAT_RGBA_8888);

    yuvReader_ = new ImageReader(&view, AIMAGE_FORMAT_YUV_420_888);
    yuvReader_->SetPresentRotation(imageRotation);
    watReader_ = new ImageReader(&wat, AIMAGE_FORMAT_RGBA_8888);
    watReader_->SetPresentRotation(imageRotation);
    //    jpgReader_ = new ImageReader(&capture, AIMAGE_FORMAT_JPEG);
    //    jpgReader_->SetPresentRotation(imageRotation);
    //    jpgReader_->RegisterCallback(this, [](void* ctx, const char* str) ->
    //    void {
    //        reinterpret_cast<CameraEngine*>(ctx)->OnPhotoTaken(str);
    //    });

    // now we could create session
    camera_->CreateSession(
        yuvReader_->GetNativeWindow(),
        /*jpgReader_->GetNativeWindow(),*/ imageRotation
    );
}

void CameraEngine::DeleteCamera(void) {
    cameraReady_ = false;
    if (camera_) {
        delete camera_;
        camera_ = nullptr;
    }
    if (yuvReader_) {
        delete yuvReader_;
        yuvReader_ = nullptr;
    }
    if (watReader_) {
        delete watReader_;
        watReader_ = nullptr;
    }
    if (jpgReader_) {
        delete jpgReader_;
        jpgReader_ = nullptr;
    }
}

ImageReader* CameraEngine::getWatImageReader() const noexcept {
    return watReader_;
}

/**
 * Initiate a Camera Run-time usage request to Java side implementation
 *  [ The request result will be passed back in function
 *    notifyCameraPermission()]
 */
// void CameraEngine::RequestCameraPermission() {
//     if (!app_) return;
//
//     JNIEnv* env;
//     ANativeActivity* activity = app_->activity;
//     activity->vm->GetEnv((void**)&env, JNI_VERSION_1_6);
//
//     activity->vm->AttachCurrentThread(&env, NULL);
//
//     jobject activityObj = env->NewGlobalRef(activity->clazz);
//     jclass clz = env->GetObjectClass(activityObj);
//     env->CallVoidMethod(activityObj,
//                         env->GetMethodID(clz, "RequestCamera", "()V"));
//     env->DeleteGlobalRef(activityObj);
//
//     activity->vm->DetachCurrentThread();
// }
/**
 * Process to user's sensitivity and exposure value change
 * all values are represented in int64_t even exposure is just int32_t
 * @param code ACAMERA_SENSOR_EXPOSURE_TIME or ACAMERA_SENSOR_SENSITIVITY
 * @param val corresponding value from user
 */
// void CameraEngine::OnCameraParameterChanged(int32_t code, int64_t val) {
//     camera_->UpdateCameraRequestParameter(code, val);
// }

/**
 * The main function rendering a frame. In our case, it is yuv to RGBA8888
 * converter
 */
void CameraEngine::DrawFrame(void) {
    if (!cameraReady_ || !yuvReader_) return;
    AImage* image = yuvReader_->GetNextImage();
    if (!image) {
        return;
    }

    LOGI("Next image acquired");

    //    ANativeWindow_acquire(app_->window);
    //    ANativeWindow_Buffer buf;
    //    if (ANativeWindow_lock(app_->window, &buf, nullptr) < 0) {
    //        yuvReader_->DeleteImage(image);
    //        return;
    //    }
    //
    //    yuvReader_->DisplayImage(&buf, image);
    //    ANativeWindow_unlockAndPost(app_->window);
    //    ANativeWindow_release(app_->window);
}

AHardwareBuffer* CameraEngine::getNextHwBuffer() {
    if (!cameraReady_ || !yuvReader_) return nullptr;
    AImage* image = yuvReader_->GetLatestImage();
    if (!image) {
        return nullptr;
    }

    LOGI("Next image acquired");
    AHardwareBuffer* hwBuffer;
    media_status_t status = AImage_getHardwareBuffer(image, &hwBuffer);

    if (status != AMEDIA_OK) {
        return nullptr;
    }

    LOGI("Hardware buffer acquired");

    AImage_delete(image);

    return hwBuffer;
}

AImage* CameraEngine::getNextCamImage() {
    if (!cameraReady_ || !yuvReader_) return nullptr;
    AImage* image = yuvReader_->GetNextImage();
    if (!image) return nullptr;
    return image;
}

AImage* CameraEngine::getNextWatImage() {
    if (!watReader_) return nullptr;
    AImage* image = watReader_->GetNextImage();
    if (!image) return nullptr;
    return image;
}

int CameraEngine::GetDisplayRotation() { return 0; }

/**
 * Handle Android System APP_CMD_INIT_WINDOW message
 *   Request camera persmission from Java side
 *   Create camera object if camera has been granted
 */
void CameraEngine::OnAppInitWindow(void) {
    rotation_ = GetDisplayRotation();

    CreateCamera();
    ASSERT(camera_, "CameraCreation Failed");

    //    EnableUI();

    // NativeActivity end is ready to display, start pulling images
    cameraReady_ = true;
    camera_->StartPreview(true);
}

/**
 * Handle APP_CMD_TEMR_WINDOW
 */
void CameraEngine::OnAppTermWindow(void) {
    cameraReady_ = false;
    DeleteCamera();
}

/**
 * Handle APP_CMD_CONFIG_CHANGED
 */
void CameraEngine::OnAppConfigChange(void) {
    int newRotation = GetDisplayRotation();

    if (newRotation != rotation_) {
        OnAppTermWindow();

        rotation_ = newRotation;
        OnAppInitWindow();
    }
}

/**
 * Retrieve saved native window width.
 * @return width of native window
 */
int32_t CameraEngine::GetSavedNativeWinWidth(void) {
    return savedNativeWinRes_.width;
}

/**
 * Retrieve saved native window height.
 * @return height of native window
 */
int32_t CameraEngine::GetSavedNativeWinHeight(void) {
    return savedNativeWinRes_.height;
}

/**
 * Retrieve saved native window format
 * @return format of native window
 */
int32_t CameraEngine::GetSavedNativeWinFormat(void) {
    return savedNativeWinRes_.format;
}

/**
 * Save original NativeWindow Resolution
 * @param w width of native window in pixel
 * @param h height of native window in pixel
 * @param format
 */
void CameraEngine::SaveNativeWinRes(int32_t w, int32_t h, int32_t format) {
    savedNativeWinRes_.width = w;
    savedNativeWinRes_.height = h;
    savedNativeWinRes_.format = format;
}
