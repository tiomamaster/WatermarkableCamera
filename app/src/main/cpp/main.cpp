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
    VkRenderer* vkRenderer = nullptr;
    ImageReader* watReader = nullptr;
    CameraManager* camMgr = nullptr;
};

struct VkCameraActivity {
    static constexpr auto Name() {
        return "com/gmail/tiomamaster/watermarkablecamera/VkCameraActivity";
    }
};
struct Surface {
    static constexpr auto Name() { return "android/view/Surface"; }
};

static const jni::Class<VkCameraActivity>* actClass = nullptr;

void init(AppState* appState, ANativeWindow* window, GameActivity* activity) {
    logI("Setting a new surface");
    appState->vkRenderer->reset(window, activity->assetManager);

    if (!appState->vkRenderer->initialized) {
        logI("Init renderer, image readers and camera manager");
        appState->vkRenderer->init();

        // get mediaSurface
        auto env = jni::AttachCurrentThread(*activity->vm);
        auto actObj = jni::Global<jni::Object<VkCameraActivity>>(
            *env, jni::Wrap<jni::jobject*>(activity->javaGameActivity)
        );
        auto mediaSurface = actObj.Get(
            *env, actClass->GetField<jni::Object<Surface>>(*env, "mediaSurface")
        );
        ANativeWindow* mediaWindow =
            ANativeWindow_fromSurface(&*env, jni::Unwrap(mediaSurface.get()));
        appState->vkRenderer->setMediaWindow(mediaWindow);

        //  setupWatermark
        jobject surface = ANativeWindow_toSurface(
            &*env, appState->watReader->getNativeWindow()
        );
        actObj.Call(
            *env,
            actClass->GetMethod<void(jni::Object<Surface>)>(
                *env, "setupWatermark"
            ),
            jni::Local<jni::Object<Surface>>(
                *env, jni::Wrap<jni::jobject*>(surface)
            )
        );
        // not sure about this, but without it app crahes
        actObj.release();
    }

    appState->camMgr->startPreview(true);
}

// Called by the Android runtime whenever events happen so the app can react to
// it.
static void handleAppCommand(android_app* app, int32_t cmd) {
    auto* appState = static_cast<AppState*>(app->userData);

    switch (cmd) {
        case APP_CMD_START:
            logI("Called - APP_CMD_START");
            break;
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            logI("Called - APP_CMD_INIT_WINDOW");
            if (app->window != nullptr) {
                init(appState, app->window, app->activity);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being hidden or closed, clean it up.
            logI("Called - APP_CMD_TERM_WINDOW");
            // todo: terminate camera, this call probably do this termination
            appState->camMgr->startPreview(false);
            appState->vkRenderer->cleanup();
            break;
        case APP_CMD_DESTROY:
            // The window is being hidden or closed, clean it up.
            logI("Destroying");
        default:
            break;
    }
}

// Android main entry point required by the Android Glue library
void android_main(android_app* app) {
    logI("Called android_main");

    AppState appState;
    VkRenderer vkRenderer;
    // todo: maybe use lambdas as callbacks instead passing vkRenderer
    ImageReader camReader(1920, 1080, AIMAGE_FORMAT_YUV_420_888, vkRenderer);
    ImageReader watReader(1080, 1920, AIMAGE_FORMAT_RGBA_8888, vkRenderer);
    CameraManager cameraManager(camReader.getNativeWindow());

    auto env = jni::AttachCurrentThread(*app->activity->vm);

    // start/stop video recording
    jni::RegisterNatives(
        *env,
        **actClass,
        jni::MakeNativeMethod(
            "nativeStartRecording",
            [&vkRenderer](jni::JNIEnv&, jni::Object<VkCameraActivity>&) {
                vkRenderer.startRecording();
            }
        ),
        jni::MakeNativeMethod(
            "nativeStopRecording",
            [&vkRenderer](jni::JNIEnv&, jni::Object<VkCameraActivity>&) {
                vkRenderer.stopRecording();
            }
        )
    );

    appState.vkRenderer = &vkRenderer;
    appState.watReader = &watReader;
    appState.camMgr = &cameraManager;
    app->userData = &appState;
    app->onAppCmd = handleAppCommand;

    int events;
    android_poll_source* source;
    while (!app->destroyRequested) {
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
        }
    }
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    logI("Called JNI_OnLoad");
    auto& env = jni::GetEnv(*vm);
    actClass = &jni::Class<VkCameraActivity>::Singleton(env);
    return jni::Unwrap(jni::jni_version_1_6);
}
