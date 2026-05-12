#include <android/native_window_jni.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <jni/jni.hpp>

#include "camera_manager.hpp"
#include "image_reader.hpp"
#include "util.hpp"
#include "vulkan_renderer.hpp"

using namespace camera;
using namespace camera::util;

struct AppState {
    android_app* androidApp = nullptr;
    VkRenderer* vkRenderer = nullptr;
    CameraManager* camMgr = nullptr;
    bool canRender = false;
};

struct VkCameraActivity {
    static constexpr auto Name() {
        return "com/gmail/tiomamaster/watermarkablecamera/VkCameraActivity";
    }
};
struct Surface {
    static constexpr auto Name() { return "android/view/Surface"; }
};

VkRenderer* vkApp;
static const jni::Class<VkCameraActivity>* actClass = nullptr;

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
                appState->vkRenderer->reset(
                    app->window, app->activity->assetManager
                );
                if (!appState->vkRenderer->initialized) {
                    logI("Starting application");
                    appState->vkRenderer->init();

                    // get mediaSurface
                    auto env = jni::AttachCurrentThread(*app->activity->vm);
                    auto actObj = jni::Global<jni::Object<VkCameraActivity>>(
                        *env,
                        jni::Wrap<jni::jobject*>(
                            app->activity->javaGameActivity
                        )
                    );
                    auto mediaSurface = actObj.Get(
                        *env,
                        actClass->GetField<jni::Object<Surface>>(
                            *env, "mediaSurface"
                        )
                    );
                    ANativeWindow* mediaWindow = ANativeWindow_fromSurface(
                        &*env, jni::Unwrap(mediaSurface.release())
                    );
                    vkApp->setMediaWindow(mediaWindow);
                }
                appState->canRender = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being hidden or closed, clean it up.
            logI("Called - APP_CMD_TERM_WINDOW");
            // todo: terminate camera, this call probably do this termination
            appState->camMgr->startPreview(false);
            appState->canRender = false;
            break;
        case APP_CMD_DESTROY:
            // The window is being hidden or closed, clean it up.
            logI("Destroying");
            appState->vkRenderer->cleanup();
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
        vkApp->camHwBufferToTexture(hwBuffer);
    } else {
        vkApp->watHwBufferToTexture(hwBuffer);
    }

    AHardwareBuffer_release(hwBuffer);
    AImage_delete(image);
}

// Android main entry point required by the Android Glue library
void android_main(android_app* app) {
    logI("Called android_main");

    AppState appState;

    VkRenderer vulkanApplication;
    vkApp = &vulkanApplication;

    ImageReader cameraReader(1920, 1080, AIMAGE_FORMAT_YUV_420_888);
    ImageReader watermarkReader(1080, 1920, AIMAGE_FORMAT_RGBA_8888);
    CameraManager cameraManager(cameraReader.getNativeWindow());

    auto env = jni::AttachCurrentThread(*app->activity->vm);
    auto actObj = jni::Local<jni::Object<VkCameraActivity>>(
        *env, jni::Wrap<jni::jobject*>(app->activity->javaGameActivity)
    );

    // setupWatermark
    jobject surface =
        ANativeWindow_toSurface(&*env, watermarkReader.getNativeWindow());
    actObj.Call(
        *env,
        actClass->GetMethod<void(jni::Object<Surface>)>(*env, "setupWatermark"),
        jni::Local<jni::Object<Surface>>(
            *env, jni::Wrap<jni::jobject*>(surface)
        )
    );

    appState.androidApp = app;
    appState.vkRenderer = vkApp;
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

void startStopRecording(JNIEnv*, jobject) {
    // vkApp->startStopRecording();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    logI("Called JNI_OnLoad");
    auto& env = jni::GetEnv(*vm);
    actClass = &jni::Class<VkCameraActivity>::Singleton(env);
    // jni::RegisterNatives(
    //     env,
    //     **actClass,
    // *jni::Class<VkCameraActivity>::Singleton(*env),
    // jni::MakeNativeMethod<decltype(&setActivity), &setActivity>(
    //     "nativeSetActivity"
    // ),
    // jni::MakeNativeMethod<
    //     decltype(&getWatermarkSurface),
    //     &getWatermarkSurface>("nativeGetWatermarkSurface")
    // jni::MakeNativeMethod<decltype(&setMediaSurface), &setMediaSurface>(
    //     "nativeSetMediaSurface"
    // )
    // );

    return jni::Unwrap(jni::jni_version_1_6);
}
