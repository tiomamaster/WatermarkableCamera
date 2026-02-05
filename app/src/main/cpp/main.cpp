#include <android/hardware_buffer_jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <jni.h>

#include "camera_engine.hpp"
#include "hellovk.hpp"
#include "image_reader.hpp"

struct AppState {
    android_app* androidApp = nullptr;
    VulkanApplication* vkApp = nullptr;
    CameraEngine* camEngine = nullptr;
    bool canRender = false;
};

VulkanApplication vkApp{};

/**
 * Called by the Android runtime whenever events happen so the
 * app can react to it.
 */
static void handleAppCommand(android_app* app, int32_t cmd) {
    auto* appState = static_cast<AppState*>(app->userData);

    switch (cmd) {
        case APP_CMD_START:
            LOGI("Called - APP_CMD_START");
            //            if (appState->androidApp->window != nullptr) {
            //                appState->vkApp->reset(app->window,
            //                app->activity->assetManager);
            //                appState->vkApp->initVulkan();
            //                appState->canRender = true;
            //            }
            break;
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            LOGI("Called - APP_CMD_INIT_WINDOW");
            if (appState->androidApp->window != nullptr) {
                LOGI("Init camera engine");
                appState->camEngine->SaveNativeWinRes(
                    ANativeWindow_getWidth(app->window),
                    ANativeWindow_getHeight(app->window),
                    ANativeWindow_getFormat(app->window)
                );
                appState->camEngine->OnAppInitWindow();

                LOGI("Setting a new surface");
                appState->vkApp->reset(
                    app->window, app->activity->assetManager
                );
                if (!appState->vkApp->initialized) {
                    LOGI("Starting application");
                    appState->vkApp->initVulkan();
                }
                appState->canRender = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // todo: terminate camera
            // The window is being hidden or closed, clean it up.
            LOGI("Called - APP_CMD_TERM_WINDOW");
            appState->canRender = false;
            break;
        case APP_CMD_DESTROY:
            // The window is being hidden or closed, clean it up.
            LOGI("Destroying");
            appState->vkApp->cleanup();
        default:
            break;
    }
}

CameraEngine* camEng;

void drawFrame(AImage* image, bool isCam) {
    if (!image) return;

    // LOGI("Next image acquired");

    AHardwareBuffer* hwBuffer;
    media_status_t status = AImage_getHardwareBuffer(image, &hwBuffer);

    if (status != AMEDIA_OK) {
        LOGE("Can't acquire hw buffer");
        AImage_delete(image);
        return;
    }

    AHardwareBuffer_acquire(hwBuffer);
    // LOGI("Buffer %p acquired by vk renderer", hwBuffer);

    if (isCam) {
        vkApp.hwBufferToTexture(hwBuffer);
    } else {
        vkApp.watHwBufferToTexture(hwBuffer);
    }

    AHardwareBuffer_release(hwBuffer);
    AImage_delete(image);
}

// Android main entry point required by the Android Glue library
[[maybe_unused]] void android_main(struct android_app* app) {
    AppState appState{};
    CameraEngine camEngine(app);
    camEng = &camEngine;

    appState.androidApp = app;
    appState.vkApp = &vkApp;
    appState.camEngine = &camEngine;
    app->userData = &appState;
    app->onAppCmd = handleAppCommand;

    int events;
    android_poll_source* source;

    while (app->destroyRequested == 0) {
        while (
            ALooper_pollOnce(
                appState.canRender ? 0 : -1, nullptr, &events, (void**)&source
            ) >= 0
        ) {
            if (source != nullptr) {
                source->process(app, source);
            }
        }

        drawFrame(camEngine.getNextCamImage(), true);
        drawFrame(camEngine.getNextWatImage(), false);
    }
}

void test(JNIEnv* env, jobject a, jobject hardwareBufferObj) {
    LOGI("test called from jvm side");
    AHardwareBuffer* hwBuffer =
        AHardwareBuffer_fromHardwareBuffer(env, hardwareBufferObj);
    LOGI(
        "AHardwareBuffer from HardwareBuffer success = %s",
        hwBuffer != nullptr ? "true" : "false"
    );
    AHardwareBuffer_acquire(hwBuffer);
    LOGI("Buffer %p acquired by vk renderer", hwBuffer);

    vkApp.watHwBufferToTexture(hwBuffer);

    AHardwareBuffer_release(hwBuffer);
}

jobject getWatermarkSurface(JNIEnv* env, jobject) {
    LOGI("getWatermarkSurface called");
    ImageReader* ir = camEng->getWatImageReader();
    ANativeWindow* nativeWindow = ir->GetNativeWindow();
    jobject surface = ANativeWindow_toSurface(env, nativeWindow);
    return surface;
}

void setMediaSurface(JNIEnv* env, jobject, jobject surface) {
    LOGI("setMediaSurface called");
    ANativeWindow* mediaWindow = ANativeWindow_fromSurface(env, surface);
    vkApp.setMediaWindow(mediaWindow);
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* _Nonnull vm, void* _Nullable) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass c = env->FindClass(
        "com/gmail/tiomamaster/watermarkablecamera/VulkanActivity"
    );
    if (c == nullptr) return JNI_ERR;

    static const JNINativeMethod methods[] = {
        {"test",
         "(Landroid/hardware/HardwareBuffer;)V",
         reinterpret_cast<void*>(test)},
        {"getWatermarkSurface",
         "()Landroid/view/Surface;",
         reinterpret_cast<jobject*>(getWatermarkSurface)},
        {"setMediaSurface",
         "(Landroid/view/Surface;)V",
         reinterpret_cast<void*>(setMediaSurface)}
    };
    int rc = env->RegisterNatives(c, methods, 3);
    if (rc != JNI_OK) return rc;

    return JNI_VERSION_1_6;
}
