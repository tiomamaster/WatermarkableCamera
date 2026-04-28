#pragma once

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadataTags.h>

#include <map>
#include <string>
#include <vector>

namespace camera {

enum class CaptureSessionState : int32_t {
    READY = 0,  // session is ready
    ACTIVE,     // session is busy
    CLOSED,     // session is closed(by itself or a new session evicts)
    MAX_STATE
};

enum PREVIEW_INDICES {
    PREVIEW_REQUEST_IDX = 0,
    JPG_CAPTURE_REQUEST_IDX,
    CAPTURE_REQUEST_COUNT,
};

struct CaptureRequestInfo {
    ANativeWindow* outputNativeWindow_;
    ACaptureSessionOutput* sessionOutput_;
    ACameraOutputTarget* target_;
    ACaptureRequest* request_;
    ACameraDevice_request_template template_;
    int sessionSequenceId_;
};

// helper classes to hold enumerated camera
struct CameraId {
    ACameraDevice* device_;
    std::string id_;
    acamera_metadata_enum_android_lens_facing_t facing_;
    bool available_;  // free to use, no other apps are using
    bool owner_;      // we are the owner of the camera

    explicit CameraId(const char* id)
        : device_(nullptr),
          id_(id),
          facing_(ACAMERA_LENS_FACING_FRONT),
          available_(false),
          owner_(false) {}

    explicit CameraId() { CameraId(""); }
};

class CameraManager {
  public:
    CameraManager(ANativeWindow* previewWindow);
    ~CameraManager();

    void onCameraStatusChanged(const char* id, bool available);
    void onDisconnected(ACameraDevice* dev);
    void onError(ACameraDevice* dev, int err);
    void onSessionState(ACameraCaptureSession* ses, CaptureSessionState state);
    void startPreview(bool start);

  private:
    ACameraManager* cameraMgr_;
    std::map<std::string, CameraId> cameras_;
    std::string activeCameraId_;
    uint32_t cameraFacing_;
    uint32_t cameraOrientation_;

    std::vector<CaptureRequestInfo> requests_;

    ACaptureSessionOutputContainer* outputContainer_;
    ACameraCaptureSession* captureSession_;
    CaptureSessionState captureSessionState_;

    ACameraManager_AvailabilityCallbacks* cameraMgrListener;

    volatile bool valid_;

    void enumerateCameras();
    void createSession(ANativeWindow* previewWindow);
    bool getSensorOrientation(int32_t* facing, int32_t* angle);
};

}  // namespace camera
