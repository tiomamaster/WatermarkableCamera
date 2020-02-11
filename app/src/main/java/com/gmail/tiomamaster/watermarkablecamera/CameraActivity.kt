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
import android.view.View
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import kotlinx.android.synthetic.main.activity_camera.*
import kotlinx.android.synthetic.main.watermark.view.*
import java.io.File
import java.lang.Long.signum
import java.util.*
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import kotlin.Comparator
import kotlin.concurrent.fixedRateTimer

class CameraActivity : AppCompatActivity(), Renderer.StateListener {

    private lateinit var renderer: Renderer

    private lateinit var watermark: WatermarkView
    private lateinit var timer: Timer
    private var recording = false
    private var cameraFacing = CameraCharacteristics.LENS_FACING_BACK

    private val cameraOpenCloseLock = Semaphore(1)

    private var backgroundThread: HandlerThread? = null
    private var backgroundHandler: Handler? = null

//    private var sensorOrientation = 0

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

    private var captureSession: CameraCaptureSession? = null

    private val rotation by lazy { windowManager.defaultDisplay.rotation }

    private val captureSessionCallback = object : CameraCaptureSession.StateCallback() {
        override fun onConfigured(session: CameraCaptureSession) {
            if (cameraDevice == null) return
            captureSession = session
            captureRequestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO)
            session.setRepeatingRequest(captureRequestBuilder.build(), null, null)

            btnStartStop.setOnClickListener(::startStopRecording)
            btnChangeCamera.setOnClickListener(::changeCamera)
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

    private val hasPermissions: Boolean
        get() = PERMISSIONS.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_camera)

        renderer = Renderer(applicationContext, this)
        rsv.rendererCallbacks = renderer
    }

    override fun onResume() {
        super.onResume()
        startBackgroundThread()
        rsv.resume()

        if (renderer.ready && cameraOpenCloseLock.availablePermits() == 1) {
            openCamera()
            setupWatermark()
        }
    }

    override fun onPause() {
        super.onPause()
        closeCamera()
        stopBackgroundThread()
        renderer.releaseSurfaceTextures()
        rsv.pause()
    }

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
        runOnUiThread {
            openCamera()
            setupWatermark()
        }
    }

    @SuppressLint("InflateParams", "Recycle")
    private fun setupWatermark() {
        renderer.setupWatermarkSurfaceTexture(rsv.width, rsv.height)
        watermark = layoutInflater.inflate(R.layout.watermark, null) as WatermarkView
        with(watermark) {
            surface = Surface(renderer.watermarkSurfaceTexture)
            val widthMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(rsv.width, View.MeasureSpec.EXACTLY)
            val heightMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(rsv.height, View.MeasureSpec.EXACTLY)
            this.widthMeasureSpec = widthMeasureSpec
            this.heightMeasureSpec = heightMeasureSpec
            measure(widthMeasureSpec, heightMeasureSpec)
            layout(0, 0, watermark.measuredWidth, watermark.measuredHeight)
            draw(null)
        }
    }

    @SuppressLint("MissingPermission")
    private fun openCamera() {
        if (!checkAndRequestPermissions()) return

        try {
            if (!cameraOpenCloseLock.tryAcquire(2500, TimeUnit.MILLISECONDS)) {
                throw RuntimeException("Time out waiting to lock camera opening.")
            }

            val cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
            val cameraId = cameraManager.cameraIdList.first {
                cameraManager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == cameraFacing
            }
            val characteristics = cameraManager.getCameraCharacteristics(cameraId)
//            sensorOrientation = characteristics[CameraCharacteristics.SENSOR_ORIENTATION] ?: 0
            val configurationMap =
                characteristics[CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP]
                    ?: throw RuntimeException("Cannot get available preview/video sizes")
            val previewSize = choosePreviewSize(
                configurationMap.getOutputSizes(SurfaceTexture::class.java),
                rsv.width,
                rsv.height
            )
            renderer.setupCameraSurfaceTextures(previewSize.width, previewSize.height)

            val screenAsp = if (rsv.width < rsv.height) rsv.width * 1.0f / rsv.height
            else rsv.height * 1.0f / rsv.width
            val previewAsp = previewSize.height * 1.0f / previewSize.width
            renderer.screenToPreviewAsp = if (screenAsp < previewAsp) screenAsp / previewAsp
            else previewAsp / screenAsp
            renderer.rotation = when (rotation) {
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

    @SuppressLint("SetTextI18n")
    private fun startStopRecording(v: View) {
        val btn = v as MaterialButton
        if (recording) {
            stopTimer()
            rsv.stopRecording()
            btn.text = "Start"
            recording = false
        } else {
//            val orientationHint = when (sensorOrientation) {
//                SENSOR_ORIENTATION_DEFAULT_DEGREES -> DEFAULT_ORIENTATIONS[rotation]
//                SENSOR_ORIENTATION_INVERSE_DEGREES -> INVERSE_ORIENTATIONS[rotation]
//                else -> 0
//            }
            try {
                val (videoWidth, videoHeight) = if (rsv.width < rsv.height) WIDTH to HEIGHT else HEIGHT to WIDTH
                rsv.initRecorder(
                    File(videoFilePath),
                    videoWidth,
                    videoHeight,
//                    orientationHint,
                    null,
                    null
                )
            } catch (e: Exception) {
                Log.e(TAG, "Couldn't re-init recording", e)
            }
            rsv.startRecording()
            startTimer()
            btn.text = "Stop"
            recording = true
        }
    }

    private fun startTimer() {
        var i = 0
        timer = fixedRateTimer(period = 1000) {
            watermark.text.text = "${i++}"
            watermark.draw(null)
        }
    }

    private fun stopTimer() = timer.cancel()

    private fun changeCamera(v: View) {
        v as MaterialButton
        cameraFacing = if (cameraFacing == CameraCharacteristics.LENS_FACING_BACK) {
            CameraCharacteristics.LENS_FACING_FRONT
        } else {
            CameraCharacteristics.LENS_FACING_BACK
        }
        closeCamera()
        renderer.releaseCameraSurfaceTexture()
        openCamera()
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
        if (!(requestCode == REQUEST_CODE_PERMISSIONS && grantResults.size > 1 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED &&
            grantResults[1] == PackageManager.PERMISSION_GRANTED)
        ) finish()
    }

    override fun onRendererFinished() = Unit

    private companion object {

        val TAG = CameraActivity::class.java.simpleName

        val PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        const val REQUEST_CODE_PERMISSIONS = 0xCA

//        const val SENSOR_ORIENTATION_DEFAULT_DEGREES = 90
//        const val SENSOR_ORIENTATION_INVERSE_DEGREES = 270
//        val DEFAULT_ORIENTATIONS = SparseIntArray().apply {
//            append(Surface.ROTATION_0, 90)
//            append(Surface.ROTATION_90, 0)
//            append(Surface.ROTATION_180, 270)
//            append(Surface.ROTATION_270, 180)
//        }
//        val INVERSE_ORIENTATIONS = SparseIntArray().apply {
//            append(Surface.ROTATION_0, 270)
//            append(Surface.ROTATION_90, 180)
//            append(Surface.ROTATION_180, 90)
//            append(Surface.ROTATION_270, 0)
//        }

        const val WIDTH = 720
        const val HEIGHT = 1280
    }
}