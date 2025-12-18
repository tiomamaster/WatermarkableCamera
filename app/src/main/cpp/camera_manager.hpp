#ifndef CAMERA_MANAGER_HPP
#define CAMERA_MANAGER_HPP

#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadataTags.h>

#include <map>
#include <string>
#include <vector>

#include "image_reader.hpp"

enum class CaptureSessionState : int32_t {
    READY = 0,  // session is ready
    ACTIVE,     // session is busy
    CLOSED,     // session is closed(by itself or a new session evicts)
    MAX_STATE
};

template <typename T>
class RangeValue {
  public:
    T min_, max_;
    /**
     * return absolute value from relative value
     * value: in percent (50 for 50%)
     */
    T value(int percent) {
        return static_cast<T>(min_ + (max_ - min_) * percent / 100);
    }
    RangeValue() { min_ = max_ = static_cast<T>(0); }

    bool Supported(void) const { return (min_ != max_); }
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

class CameraId;
class NDKCamera {
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

    // set up exposure control
    int64_t exposureTime_;
    RangeValue<int64_t> exposureRange_;
    int32_t sensitivity_;
    RangeValue<int32_t> sensitivityRange_;
    volatile bool valid_;

    ACameraManager_AvailabilityCallbacks* GetManagerListener();
    ACameraDevice_stateCallbacks* GetDeviceListener();
    ACameraCaptureSession_stateCallbacks* GetSessionListener();
    ACameraCaptureSession_captureCallbacks* GetCaptureCallback();

  public:
    NDKCamera();
    ~NDKCamera();
    void EnumerateCamera(void);
    bool MatchCaptureSizeRequest(
        ANativeWindow* display, ImageFormat* view, ImageFormat* capture
    );
    void CreateSession(
        ANativeWindow* previewWindow, /*ANativeWindow* jpgWindow,*/
        int32_t imageRotation
    );
    bool GetSensorOrientation(int32_t* facing, int32_t* angle);
    void OnCameraStatusChanged(const char* id, bool available);
    void OnDeviceState(ACameraDevice* dev);
    void OnDeviceError(ACameraDevice* dev, int err);
    void OnSessionState(ACameraCaptureSession* ses, CaptureSessionState state);
    void OnCaptureSequenceEnd(
        ACameraCaptureSession* session, int sequenceId, int64_t frameNumber
    );
    void OnCaptureFailed(
        ACameraCaptureSession* session,
        ACaptureRequest* request,
        ACameraCaptureFailure* failure
    );
    void StartPreview(bool start);
    bool TakePhoto(void);
    bool GetExposureRange(int64_t* min, int64_t* max, int64_t* curVal);
    bool GetSensitivityRange(int64_t* min, int64_t* max, int64_t* curVal);

    void UpdateCameraRequestParameter(int32_t code, int64_t val);
};

// helper classes to hold enumerated camera
class CameraId {
  public:
    ACameraDevice* device_;
    std::string id_;
    acamera_metadata_enum_android_lens_facing_t facing_;
    bool available_;  // free to use ( no other apps are using
    bool owner_;      // we are the owner of the camera
    explicit CameraId(const char* id)
        : device_(nullptr),
          facing_(ACAMERA_LENS_FACING_FRONT),
          available_(false),
          owner_(false) {
        id_ = id;
    }

    explicit CameraId(void) { CameraId(""); }
};

#endif  // CAMERA_MANAGER_HPP
