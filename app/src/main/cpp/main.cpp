#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "hellovk.hpp"

struct AppState {
    android_app *androidApp = nullptr;
    VulkanApplication *vkApp = nullptr;
    bool canRender = false;
};

/**
 * Called by the Android runtime whenever events happen so the
 * app can react to it.
 */
static void handleAppCommand(android_app *app, int32_t cmd) {
    auto *appState = static_cast<AppState *>(app->userData);

    switch (cmd) {
        case APP_CMD_START:
            LOGI("Called - APP_CMD_START");
//            if (appState->androidApp->window != nullptr) {
//                appState->vkApp->reset(app->window, app->activity->assetManager);
//                appState->vkApp->initVulkan();
//                appState->canRender = true;
//            }
            break;
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            LOGI("Called - APP_CMD_INIT_WINDOW");
            if (appState->androidApp->window != nullptr) {
                LOGI("Setting a new surface");
                appState->vkApp->reset(app->window, app->activity->assetManager);
                if (!appState->vkApp->initialized) {
                    LOGI("Starting application");
                    appState->vkApp->initVulkan();
                }
                appState->canRender = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
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
[[maybe_unused]] void android_main(struct android_app *app) {
    AppState appState{};
    VulkanApplication vkApp{};

    appState.androidApp = app;
    appState.vkApp = &vkApp;
    app->userData = &appState;
    app->onAppCmd = handleAppCommand;

    int events;
    android_poll_source *source;

    while (app->destroyRequested == 0) {
        while (ALooper_pollOnce(appState.canRender ? 0 : -1, nullptr, &events, (void **) &source) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
        }

        appState.vkApp->drawFrame();
    }
}