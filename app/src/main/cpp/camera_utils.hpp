#ifndef CAMERA_UTILS_HPP
#define CAMERA_UTILS_HPP

#include <camera/NdkCameraError.h>
#include <camera/NdkCameraManager.h>

/*
 * A set of macros to call into Camera APIs. The API is grouped with a few
 * objects, with object name as the prefix of function names.
 */
#define CALL_CAMERA(func)                        \
    {                                            \
        camera_status_t status = func;           \
        ASSERT(                                  \
            status == ACAMERA_OK,                \
            "%s call failed with code: %#x, %s", \
            __FUNCTION__,                        \
            status,                              \
            GetErrorStr(status)                  \
        );                                       \
    }
#define CALL_MGR(func) CALL_CAMERA(ACameraManager_##func)
#define CALL_DEV(func) CALL_CAMERA(ACameraDevice_##func)
#define CALL_METADATA(func) CALL_CAMERA(ACameraMetadata_##func)
#define CALL_CONTAINER(func) CALL_CAMERA(ACaptureSessionOutputContainer_##func)
#define CALL_OUTPUT(func) CALL_CAMERA(ACaptureSessionOutput_##func)
#define CALL_TARGET(func) CALL_CAMERA(ACameraOutputTarget_##func)
#define CALL_REQUEST(func) CALL_CAMERA(ACaptureRequest_##func)
#define CALL_SESSION(func) CALL_CAMERA(ACameraCaptureSession_##func)

/*
 * A few debugging functions for error code strings etc
 */
const char* GetErrorStr(camera_status_t err);
const char* GetTagStr(acamera_metadata_tag_t tag);
void PrintMetadataTags(int32_t entries, const uint32_t* pTags);
void PrintLensFacing(ACameraMetadata_const_entry& lensData);
void PrintCameras(ACameraManager* cameraMgr);
void PrintCameraDeviceError(int err);

void PrintRequestMetadata(ACaptureRequest* req);

#endif  // CAMERA_UTILS_HPP
