#include <android/hardware_buffer_jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>

#include "camera_manager.hpp"
#include "hellovk.hpp"
#include "image_reader.hpp"

using namespace camera;

struct AppState {
    android_app* androidApp = nullptr;
    VulkanApplication* vkApp = nullptr;
    CameraManager* camMgr = nullptr;
    bool canRender = false;
};

VulkanApplication* vkApp;
ImageReader* watReader;

/**
 * Called by the Android runtime whenever events happen so the
 * app can react to it.
 */
static void handleAppCommand(android_app* app, int32_t cmd) {
    auto* appState = static_cast<AppState*>(app->userData);

    switch (cmd) {
        case APP_CMD_START:
            logI("Called - APP_CMD_START");
            break;
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            logI("Called - APP_CMD_INIT_WINDOW");
            if (appState->androidApp->window != nullptr) {
                logI("Init camera engine");
                appState->camMgr->startPreview(true);

                logI("Setting a new surface");
                appState->vkApp->reset(
                    app->window, app->activity->assetManager
                );
                if (!appState->vkApp->initialized) {
                    logI("Starting application");
                    appState->vkApp->initVulkan();
                }
                appState->canRender = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // todo: terminate camera
            // The window is being hidden or closed, clean it up.
            logI("Called - APP_CMD_TERM_WINDOW");
            appState->canRender = false;
            break;
        case APP_CMD_DESTROY:
            // The window is being hidden or closed, clean it up.
            logI("Destroying");
            appState->vkApp->cleanup();
        default:
            break;
    }
}

void drawFrame(AImage* image, bool isCam) {
    if (!image) return;

    // logI("Next image acquired");

    AHardwareBuffer* hwBuffer;
    media_status_t status = AImage_getHardwareBuffer(image, &hwBuffer);

    if (status != AMEDIA_OK) {
        logE("Can't acquire hw buffer");
        AImage_delete(image);
        return;
    }

    AHardwareBuffer_acquire(hwBuffer);
    // logI("Buffer %p acquired by vk renderer", hwBuffer);

    if (isCam) {
        vkApp->hwBufferToTexture(hwBuffer);
    } else {
        vkApp->watHwBufferToTexture(hwBuffer);
    }

    AHardwareBuffer_release(hwBuffer);
    AImage_delete(image);
}

// Android main entry point required by the Android Glue library
[[maybe_unused]] void android_main(struct android_app* app) {
    logI("Called android_main");

    AppState appState;

    VulkanApplication vulkanApplication;
    vkApp = &vulkanApplication;

    ImageReader cameraReader(1920, 1080, AIMAGE_FORMAT_YUV_420_888);
    ImageReader watermarkReader(1080, 1920, AIMAGE_FORMAT_RGBA_8888);
    watReader = &watermarkReader;
    CameraManager cameraManager(cameraReader.getNativeWindow());

    appState.androidApp = app;
    appState.vkApp = vkApp;
    appState.camMgr = &cameraManager;
    app->userData = &appState;
    app->onAppCmd = handleAppCommand;

    int events;
    android_poll_source* source;

    while (app->destroyRequested == 0) {
        while (
            ALooper_pollOnce(
                appState.canRender ? 0 : -1, nullptr, &events, (void**)&source
            ) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
        }

        drawFrame(cameraReader.getNextImage(), true);
        drawFrame(watermarkReader.getNextImage(), false);
    }
}

jobject getWatermarkSurface(JNIEnv* env, jobject) {
    logI("getWatermarkSurface called");
    ANativeWindow* nativeWindow = watReader->getNativeWindow();
    jobject surface = ANativeWindow_toSurface(env, nativeWindow);
    return surface;
}

void setMediaSurface(JNIEnv* env, jobject, jobject surface) {
    logI("setMediaSurface called");
    ANativeWindow* mediaWindow = ANativeWindow_fromSurface(env, surface);
    vkApp->setMediaWindow(mediaWindow);
}

void nativeStartStopRecording(JNIEnv*, jobject) { vkApp->startStopRecording(); }

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* _Nonnull vm, void* _Nullable) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass c = env->FindClass(
        "com/gmail/tiomamaster/watermarkablecamera/VkCameraActivity"
    );
    if (c == nullptr) return JNI_ERR;

    static const JNINativeMethod methods[] = {
        {"getWatermarkSurface",
         "()Landroid/view/Surface;",
         reinterpret_cast<jobject*>(getWatermarkSurface)},
        {"setMediaSurface",
         "(Landroid/view/Surface;)V",
         reinterpret_cast<void*>(setMediaSurface)},
        {"nativeStartStopRecording",
         "()V",
         reinterpret_cast<void*>(nativeStartStopRecording)}
    };
    int rc = env->RegisterNatives(c, methods, 3);
    if (rc != JNI_OK) return rc;

    return JNI_VERSION_1_6;
}
