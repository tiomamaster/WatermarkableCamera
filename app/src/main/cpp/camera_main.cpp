#include "camera_engine.hpp"
#include "native_debug.hpp"

/*
 * SampleEngine global object
 */
static CameraEngine* pEngineObj = nullptr;
CameraEngine* GetAppEngine(void) {
    ASSERT(pEngineObj, "AppEngine has not initialized");
    return pEngineObj;
}

/**
 * Teamplate function for NativeActivity derived applications
 *   Create/Delete camera object with
 *   INIT_WINDOW/TERM_WINDOW command, ignoring other event.
 */
static void ProcessAndroidCmd(struct android_app* app, int32_t cmd) {
    CameraEngine* engine = reinterpret_cast<CameraEngine*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (engine->AndroidApp()->window != NULL) {
                engine->SaveNativeWinRes(
                    ANativeWindow_getWidth(app->window),
                    ANativeWindow_getHeight(app->window),
                    ANativeWindow_getFormat(app->window)
                );
                engine->OnAppInitWindow();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            engine->OnAppTermWindow();
            ANativeWindow_setBuffersGeometry(
                app->window,
                engine->GetSavedNativeWinWidth(),
                engine->GetSavedNativeWinHeight(),
                engine->GetSavedNativeWinFormat()
            );
            break;
        case APP_CMD_CONFIG_CHANGED:
            engine->OnAppConfigChange();
            break;
        case APP_CMD_LOST_FOCUS:
            break;
    }
}

extern "C" void android_main(struct android_app* state) {
    CameraEngine engine(state);
    pEngineObj = &engine;

    state->userData = reinterpret_cast<void*>(&engine);
    state->onAppCmd = ProcessAndroidCmd;

    // loop waiting for stuff to do.
    while (!state->destroyRequested) {
        struct android_poll_source* source = nullptr;
        auto result = ALooper_pollOnce(0, NULL, nullptr, (void**)&source);
        ASSERT(
            result != ALOOPER_POLL_ERROR, "ALooper_pollOnce returned an error"
        );
        if (source != NULL) {
            source->process(state, source);
        }
        pEngineObj->DrawFrame();
    }

    LOGI("CameraEngine thread destroy requested!");
    engine.DeleteCamera();
    pEngineObj = nullptr;
}
