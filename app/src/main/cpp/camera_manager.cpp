#include "camera_manager.hpp"

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <unistd.h>

#include "util.hpp"

using namespace camera::util;

namespace camera {

void onDisconnected(void* ctx, ACameraDevice* dev) {
    reinterpret_cast<CameraManager*>(ctx)->onDisconnected(dev);
}

void onError(void* ctx, ACameraDevice* dev, int err) {
    reinterpret_cast<CameraManager*>(ctx)->onError(dev, err);
}

void CameraManager::onDisconnected(ACameraDevice* dev) {
    std::string id(ACameraDevice_getId(dev));
    logW("device %s is disconnected", id.c_str());

    cameras_[id].available_ = false;
    ACameraDevice_close(cameras_[id].device_);
    cameras_.erase(id);
}

void CameraManager::onError(ACameraDevice* dev, int err) {
    std::string id(ACameraDevice_getId(dev));

    logI("CameraDevice %s is in error %#x", id.c_str(), err);
    printCameraDeviceError(err);

    CameraId& cam = cameras_[id];

    switch (err) {
        case ERROR_CAMERA_IN_USE:
        case ERROR_CAMERA_SERVICE:
        case ERROR_CAMERA_DEVICE:
        case ERROR_CAMERA_DISABLED:
        case ERROR_MAX_CAMERAS_IN_USE:
            cam.available_ = false;
            cam.owner_ = false;
            break;
        default:
            logI("Unknown Camera Device Error: %#x", err);
    }
}

void onCameraAvailable(void* ctx, const char* id) {
    reinterpret_cast<CameraManager*>(ctx)->onCameraStatusChanged(id, true);
}

void onCameraUnavailable(void* ctx, const char* id) {
    reinterpret_cast<CameraManager*>(ctx)->onCameraStatusChanged(id, false);
}

void CameraManager::onCameraStatusChanged(const char* id, bool available) {
    if (valid_) cameras_[std::string(id)].available_ = available;
}

void onSessionClosed(void* ctx, ACameraCaptureSession* ses) {
    logW("session %p closed", ses);
    reinterpret_cast<CameraManager*>(ctx)->onSessionState(
        ses, CaptureSessionState::CLOSED
    );
}

void onSessionReady(void* ctx, ACameraCaptureSession* ses) {
    logW("session %p ready", ses);
    reinterpret_cast<CameraManager*>(ctx)->onSessionState(
        ses, CaptureSessionState::READY
    );
}

void onSessionActive(void* ctx, ACameraCaptureSession* ses) {
    logW("session %p active", ses);
    reinterpret_cast<CameraManager*>(ctx)->onSessionState(
        ses, CaptureSessionState::ACTIVE
    );
}

void CameraManager::onSessionState(
    ACameraCaptureSession* ses, CaptureSessionState state
) {
    if (!ses || ses != captureSession_) {
        logW("CaptureSession is %s", (ses ? "NOT our session" : "NULL"));
        return;
    }

    logAssert(state < CaptureSessionState::MAX_STATE, "wrong session state");

    captureSessionState_ = state;
}

CameraManager::CameraManager(ANativeWindow* previewWindow)
    : cameraMgr_(nullptr),
      activeCameraId_(""),
      cameraFacing_(ACAMERA_LENS_FACING_BACK),
      cameraOrientation_(0),
      outputContainer_(nullptr),
      captureSessionState_(CaptureSessionState::MAX_STATE) {
    valid_ = false;
    requests_.resize(/*CAPTURE_REQUEST_COUNT*/ 1);
    memset(requests_.data(), 0, requests_.size() * sizeof(requests_[0]));
    cameras_.clear();
    cameraMgr_ = ACameraManager_create();
    logAssert(cameraMgr_, "failed to create cameraManager");

    // Pick up a back-facing camera to preview
    enumerateCameras();
    logAssert(activeCameraId_.size(), "unknown ActiveCameraIdx");

    // Create back facing camera device
    static ACameraDevice_StateCallbacks cameraDeviceListener = {
        .context = this,
        .onDisconnected = ::camera::onDisconnected,
        .onError = ::camera::onError
    };
    callCamera(ACameraManager_openCamera(
        cameraMgr_,
        activeCameraId_.c_str(),
        &cameraDeviceListener,
        &cameras_[activeCameraId_].device_
    ));

    static ACameraManager_AvailabilityCallbacks callbacks{
        .context = this,
        .onCameraAvailable = ::camera::onCameraAvailable,
        .onCameraUnavailable = ::camera::onCameraUnavailable,
    };
    cameraMgrListener = &callbacks;
    callCamera(ACameraManager_registerAvailabilityCallback(
        cameraMgr_, cameraMgrListener
    ));

    valid_ = true;

    createSession(previewWindow);
}

CameraManager::~CameraManager() {
    valid_ = false;
    // stop session if it is on:
    if (captureSessionState_ == CaptureSessionState::ACTIVE) {
        ACameraCaptureSession_stopRepeating(captureSession_);
    }
    ACameraCaptureSession_close(captureSession_);

    for (auto& req : requests_) {
        callCamera(ACaptureRequest_removeTarget(req.request_, req.target_));
        ACaptureRequest_free(req.request_);
        ACameraOutputTarget_free(req.target_);

        callCamera(ACaptureSessionOutputContainer_remove(
            outputContainer_, req.sessionOutput_
        ));
        ACaptureSessionOutput_free(req.sessionOutput_);

        ANativeWindow_release(req.outputNativeWindow_);
    }

    requests_.resize(0);
    ACaptureSessionOutputContainer_free(outputContainer_);

    for (auto& cam : cameras_) {
        if (cam.second.device_) {
            callCamera(ACameraDevice_close(cam.second.device_));
        }
    }
    cameras_.clear();
    if (cameraMgr_) {
        callCamera(ACameraManager_unregisterAvailabilityCallback(
            cameraMgr_, cameraMgrListener
        ));
        ACameraManager_delete(cameraMgr_);
        cameraMgr_ = nullptr;
    }
}

void CameraManager::enumerateCameras() {
    ACameraIdList* cameraIds = nullptr;
    callCamera(ACameraManager_getCameraIdList(cameraMgr_, &cameraIds));

    for (int i = 0; i < cameraIds->numCameras; ++i) {
        const char* id = cameraIds->cameraIds[i];

        ACameraMetadata* metadataObj;
        callCamera(ACameraManager_getCameraCharacteristics(
            cameraMgr_, id, &metadataObj
        ));

        int32_t count = 0;
        const uint32_t* tags = nullptr;
        ACameraMetadata_getAllTags(metadataObj, &count, &tags);
        for (int tagIdx = 0; tagIdx < count; ++tagIdx) {
            if (ACAMERA_LENS_FACING == tags[tagIdx]) {
                ACameraMetadata_const_entry lensInfo = {};
                callCamera(ACameraMetadata_getConstEntry(
                    metadataObj, tags[tagIdx], &lensInfo
                ));
                CameraId cam(id);
                cam.facing_ =
                    static_cast<acamera_metadata_enum_android_lens_facing_t>(
                        lensInfo.data.u8[0]
                    );
                cam.owner_ = false;
                cam.device_ = nullptr;
                cameras_[cam.id_] = cam;
                if (cam.facing_ == ACAMERA_LENS_FACING_BACK) {
                    activeCameraId_ = cam.id_;
                }
                break;
            }
        }
        ACameraMetadata_free(metadataObj);
    }

    logAssert(!cameras_.empty(), "no camera available on the device");
    if (activeCameraId_.empty()) {
        // if no back facing camera found, pick up the first one to use...
        activeCameraId_ = cameras_.begin()->second.id_;
    }
    ACameraManager_deleteCameraIdList(cameraIds);
}

void CameraManager::createSession(ANativeWindow* previewWindow) {
    // Create output from this app's ANativeWindow, and add into output
    // container
    requests_[PREVIEW_REQUEST_IDX].outputNativeWindow_ = previewWindow;
    requests_[PREVIEW_REQUEST_IDX].template_ = TEMPLATE_RECORD;

    callCamera(ACaptureSessionOutputContainer_create(&outputContainer_));
    for (auto& req : requests_) {
        ANativeWindow_acquire(req.outputNativeWindow_);
        callCamera(ACaptureSessionOutput_create(
            req.outputNativeWindow_, &req.sessionOutput_
        ));
        callCamera(ACaptureSessionOutputContainer_add(
            outputContainer_, req.sessionOutput_
        ));
        callCamera(
            ACameraOutputTarget_create(req.outputNativeWindow_, &req.target_)
        );
        callCamera(ACameraDevice_createCaptureRequest(
            cameras_[activeCameraId_].device_, req.template_, &req.request_
        ));
        callCamera(ACaptureRequest_addTarget(req.request_, req.target_));
    }

    // Create a capture session for the given preview request
    captureSessionState_ = CaptureSessionState::READY;
    static ACameraCaptureSession_stateCallbacks sessionListener = {
        .context = this,
        .onClosed = ::camera::onSessionClosed,
        .onReady = ::camera::onSessionReady,
        .onActive = ::camera::onSessionActive,
    };
    callCamera(ACameraDevice_createCaptureSession(
        cameras_[activeCameraId_].device_,
        outputContainer_,
        &sessionListener,
        &captureSession_
    ));

    uint8_t aeModeOn = ACAMERA_CONTROL_AE_MODE_ON;
    callCamera(ACaptureRequest_setEntry_u8(
        requests_[PREVIEW_REQUEST_IDX].request_,
        ACAMERA_CONTROL_AE_MODE,
        1,
        &aeModeOn
    ));
}

bool CameraManager::getSensorOrientation(int32_t* facing, int32_t* angle) {
    if (!cameraMgr_) {
        return false;
    }

    ACameraMetadata* metadataObj;
    ACameraMetadata_const_entry face, orientation;
    callCamera(ACameraManager_getCameraCharacteristics(
        cameraMgr_, activeCameraId_.c_str(), &metadataObj
    ));
    callCamera(
        ACameraMetadata_getConstEntry(metadataObj, ACAMERA_LENS_FACING, &face)
    );
    cameraFacing_ = static_cast<int32_t>(face.data.u8[0]);

    callCamera(ACameraMetadata_getConstEntry(
        metadataObj, ACAMERA_SENSOR_ORIENTATION, &orientation
    ));

    logI("====Current SENSOR_ORIENTATION: %8d", orientation.data.i32[0]);

    ACameraMetadata_free(metadataObj);
    cameraOrientation_ = orientation.data.i32[0];

    if (facing) *facing = cameraFacing_;
    if (angle) *angle = cameraOrientation_;
    return true;
}

void CameraManager::startPreview(bool start) {
    if (start) {
        callCamera(ACameraCaptureSession_setRepeatingRequest(
            captureSession_,
            nullptr,
            1,
            &requests_[PREVIEW_REQUEST_IDX].request_,
            nullptr
        ));
    } else if (!start && captureSessionState_ == CaptureSessionState::ACTIVE) {
        ACameraCaptureSession_stopRepeating(captureSession_);
    } else {
        logAssert(false, "conflict states");
    }
}

}  // namespace camera
