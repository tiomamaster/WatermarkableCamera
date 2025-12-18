#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "camera_engine.hpp"
#include "hellovk.hpp"

struct AppState {
    android_app* androidApp = nullptr;
    VulkanApplication* vkApp = nullptr;
    CameraEngine* camEngine = nullptr;
    bool canRender = false;
};

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

// Android main entry point required by the Android Glue library
[[maybe_unused]] void android_main(struct android_app* app) {
    AppState appState{};
    VulkanApplication vkApp{};
    CameraEngine camEngine(app);

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

        AImage* image = camEngine.getNextImage();
        if (!image) continue;

        LOGI("Next image acquired");

        AHardwareBuffer* hwBuffer;
        media_status_t status = AImage_getHardwareBuffer(image, &hwBuffer);

        if (status != AMEDIA_OK) {
            LOGE("Can't acquire hw buffer");
            AImage_delete(image);
            continue;
        }

        AHardwareBuffer_acquire(hwBuffer);
        LOGI("Buffer %p acquired by vk renderer", hwBuffer);

        vkApp.hwBufferToTexture(hwBuffer);

        AHardwareBuffer_release(hwBuffer);
        AImage_delete(image);
    }
}
