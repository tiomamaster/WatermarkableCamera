package com.gmail.tiomamaster.watermarkablecamera

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Size
import android.view.Surface
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import kotlinx.android.synthetic.main.activity_camera.*
import java.io.File
import java.lang.Long.signum
import java.util.*
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import kotlin.Comparator

class CameraActivity : AppCompatActivity(), Renderer.StateListener {

    private lateinit var renderer: Renderer

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera)

        renderer = Renderer(applicationContext, this)
        rsv.rendererCallbacks = renderer
    }

    private val cameraOpenCloseLock = Semaphore(1)

    override fun onResume() {
        super.onResume()
        startBackgroundThread()
        rsv.resume()

        if (renderer.ready && cameraOpenCloseLock.availablePermits() == 1) openCamera()
    }

    override fun onPause() {
        super.onPause()
        closeCamera()
        stopBackgroundThread()
        renderer.releaseSurfaceTextures()
        rsv.pause()
    }

    private var backgroundThread: HandlerThread? = null
    private var backgroundHandler: Handler? = null

    private fun startBackgroundThread() {
        backgroundThread = HandlerThread("CameraBackground")
        backgroundThread?.start()
        backgroundHandler = Handler(backgroundThread!!.looper)
    }

    private fun stopBackgroundThread() {
        backgroundThread?.quitSafely()
        try {
            backgroundThread?.join()
            backgroundThread = null
            backgroundHandler = null
        } catch (e: InterruptedException) {
            Log.e(TAG, e.toString())
        }
    }

    override fun onRendererReady() {
        runOnUiThread { openCamera() }
    }

    @SuppressLint("MissingPermission")
    private fun openCamera() {
        if (!checkAndRequestPermissions()) return

        try {
            if (!cameraOpenCloseLock.tryAcquire(2500, TimeUnit.MILLISECONDS)) {
                throw RuntimeException("Time out waiting to lock camera opening.")
            }

            val cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
            val cameraId = cameraManager.cameraIdList.first()
            val characteristics = cameraManager.getCameraCharacteristics(cameraId)
            val configurationMap =
                characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                    ?: throw RuntimeException("Cannot get available preview/video sizes")
            val previewSize = choosePreviewSize(
                configurationMap.getOutputSizes(SurfaceTexture::class.java),
                rsv.width,
                rsv.height
            )
            renderer.setupSurfaceTextures(
                previewSize.width,
                previewSize.height,
                rsv.width,
                rsv.height
            )

            val screenAsp = if (rsv.width < rsv.height) rsv.width * 1.0f / rsv.height
            else rsv.height * 1.0f / rsv.width
            val previewAsp = previewSize.height * 1.0f / previewSize.width
            renderer.screenToPreviewAsp = if (screenAsp < previewAsp) screenAsp / previewAsp
            else previewAsp / screenAsp
            renderer.rotation = when (windowManager.defaultDisplay.rotation) {
                Surface.ROTATION_90 -> 90f
                Surface.ROTATION_270 -> -90f
                else -> 0f
            }

            cameraManager.openCamera(cameraId, cameraStateCallback, backgroundHandler)
        } catch (e: CameraAccessException) {
            Toast.makeText(this, "Cannot access the camera.", Toast.LENGTH_SHORT).show()
            finish()
        } catch (e: NullPointerException) {
            // Currently an NPE is thrown when the Camera2API is used but not supported on the device this code runs.
            Toast.makeText(this, "Camera2API is not supported.", Toast.LENGTH_SHORT).show()
            finish()
        } catch (e: InterruptedException) {
            throw RuntimeException("Interrupted while trying to lock camera opening.")
        }
    }

    private fun closeCamera() {
        try {
            cameraOpenCloseLock.acquire()
            closePreviewSession()
            cameraDevice?.close()
            cameraDevice = null
        } catch (e: InterruptedException) {
            throw RuntimeException("Interrupted while trying to lock camera closing.", e)
        } finally {
            cameraOpenCloseLock.release()
        }
    }

    private var cameraDevice: CameraDevice? = null

    private val cameraStateCallback = object : CameraDevice.StateCallback() {
        override fun onOpened(camera: CameraDevice) {
            cameraOpenCloseLock.release()
            cameraDevice = camera
            startPreview()
        }

        override fun onDisconnected(camera: CameraDevice) {
            cameraOpenCloseLock.release()
            camera.close()
            cameraDevice = null
        }

        override fun onError(camera: CameraDevice, error: Int) {
            cameraOpenCloseLock.release()
            camera.close()
            cameraDevice = null
            finish()
        }
    }

    private lateinit var captureRequestBuilder: CaptureRequest.Builder

    private fun startPreview() {
        try {
            closePreviewSession()

            captureRequestBuilder =
                cameraDevice!!.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)

            val surface = Surface(renderer.cameraSurfaceTexture)
            captureRequestBuilder.addTarget(surface)
            cameraDevice?.createCaptureSession(
                listOf(surface),
                captureSessionCallback,
                backgroundHandler
            )
        } catch (e: CameraAccessException) {
            Log.e(TAG, e.toString())
        }
    }

    private fun closePreviewSession() {
        captureSession?.close()
        captureSession = null
    }

    private var captureSession: CameraCaptureSession? = null

    private val captureSessionCallback = object : CameraCaptureSession.StateCallback() {
        override fun onConfigured(session: CameraCaptureSession) {
            captureSession = session
            captureRequestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO)
            session.setRepeatingRequest(captureRequestBuilder.build(), null, null)
        }

        override fun onConfigureFailed(session: CameraCaptureSession) = Unit
    }

    private val videoFilePath: String
        get() {
            val filename = "${System.currentTimeMillis()}.mp4"
            val dir = getExternalFilesDir(null)

            return if (dir == null) {
                filename
            } else {
                "${dir.absolutePath}/$filename"
            }
        }

    @Suppress("SameParameterValue")
    private fun choosePreviewSize(
        choices: Array<Size>,
        width: Int,
        height: Int
    ): Size {
        // Collect the supported resolutions that are at least as big as the preview Surface
        val bigEnough = choices.filter {
            it.height == it.width * height / width && it.width >= width && it.height >= height
        }

        // Pick the smallest of those, assuming we found any
        return if (bigEnough.isNotEmpty()) {
            val comparator =
                Comparator<Size> { lhs, rhs ->
                    signum(lhs.width.toLong() * lhs.height - rhs.width.toLong() * rhs.height)
                }
            Collections.min(bigEnough, comparator)
        } else {
            choices[0]
        }
    }

    private val hasPermissions: Boolean
        get() = PERMISSIONS.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    private fun checkAndRequestPermissions(): Boolean =
        if (!hasPermissions) {
            requestPermissions(PERMISSIONS, REQUEST_CODE_PERMISSIONS)
            false
        } else {
            true
        }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_CODE_PERMISSIONS && grantResults.size > 1 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED &&
            grantResults[1] == PackageManager.PERMISSION_GRANTED
        ) openCamera() else finish()
    }

    override fun onRendererFinished() = Unit

    private companion object {

        val TAG = CameraActivity::class.java.simpleName

        val PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        const val REQUEST_CODE_PERMISSIONS = 0xCA

        const val WIDTH = 720
        const val HEIGHT = 1280
    }
}