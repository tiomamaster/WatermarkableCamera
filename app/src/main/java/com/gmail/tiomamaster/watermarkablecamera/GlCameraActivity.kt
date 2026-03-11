package com.gmail.tiomamaster.watermarkablecamera

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.OrientationEventListener
import android.view.Surface
import android.view.View
import android.view.WindowManager
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.gmail.tiomamaster.watermarkablecamera.databinding.ActivityCameraGlBinding
import com.gmail.tiomamaster.watermarkablecamera.databinding.WatermarkBinding
import com.google.android.material.button.MaterialButton
import java.io.File
import java.lang.Long.signum
import java.util.Collections
import java.util.Timer
import java.util.concurrent.Executors
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import kotlin.concurrent.fixedRateTimer
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.random.Random

class GlCameraActivity : AppCompatActivity(), Renderer.StateListener {

    private lateinit var binding: ActivityCameraGlBinding
    private lateinit var watBinding: WatermarkBinding

    private lateinit var renderer: Renderer

    private lateinit var timer: Timer
    private var recording = false

    private var cameraFacing = CameraCharacteristics.LENS_FACING_BACK
    private val cameraOpenCloseLock = Semaphore(1)

    private var backgroundThread: HandlerThread? = null
    private var backgroundHandler: Handler? = null

    private var sensorOrientation = 0
    private var screenRotation = 0

    private val orientationEventListener by lazy {
        object : OrientationEventListener(this) {
            val desired = arrayOf(0, 90, 180, 270, 360)
            override fun onOrientationChanged(orientation: Int) {
                screenRotation = desired.map { abs(it - orientation) }.run {
                    desired[indexOf(min())].let {
                        if (it == 360) 0 else it
                    }
                }
            }
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

    private var captureSession: CameraCaptureSession? = null

    private val captureSessionCallback = object : CameraCaptureSession.StateCallback() {
        override fun onConfigured(session: CameraCaptureSession) {
            if (cameraDevice == null) return
            captureSession = session
            captureRequestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO)
            captureRequestBuilder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, Range(30, 30))
            session.setRepeatingRequest(captureRequestBuilder.build(), null, backgroundHandler)

            binding.btnStartStop.setOnClickListener(::startStopRecording)
            binding.btnChangeCamera.setOnClickListener(::changeCamera)
        }

        override fun onConfigureFailed(session: CameraCaptureSession) = Unit
    }

    private val videoFilePath: String
        get() {
            val filename = "${System.currentTimeMillis()}.mp4"
            val dir = "/sdcard/DCIM/Camera"
            return "$dir/$filename"
        }

    private var resolution = Resolution.FHD

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityCameraGlBinding.inflate(layoutInflater)
        setContentView(binding.root)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(
                WindowInsetsCompat.Type.statusBars()
                        or WindowInsetsCompat.Type.navigationBars()
                        or WindowInsetsCompat.Type.systemBars()
                        or WindowInsetsCompat.Type.displayCutout()
            )
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        // adjust video's preview size to make it aspect ratio equal to recorded video
        val height = resources.displayMetrics.heightPixels
        val width = resources.displayMetrics.widthPixels
        resolution = Resolution.entries[intent.getIntExtra(MainActivity.EXTRA_RESOLUTION, 1)]
        val asp = resolution.size.width / resolution.size.height.toFloat()
        if (height > width) {
            binding.rsv.layoutParams.height = (width * asp).roundToInt()
        } else {
            binding.rsv.layoutParams.width = (height * asp).roundToInt()
        }

        renderer = Renderer(applicationContext, this)
        binding.rsv.renderer = renderer
    }

    override fun onResume() {
        super.onResume()
        startBackgroundThread()
        binding.rsv.resume()
        orientationEventListener.enable()
    }

    override fun onPause() {
        super.onPause()
        closeCamera()
        stopBackgroundThread()
        binding.rsv.pause()
        orientationEventListener.disable()
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

    private fun setupWatermark() {
        renderer.setupWatermarkSurfaceTexture(binding.rsv.width, binding.rsv.height)
        watBinding = WatermarkBinding.inflate(layoutInflater)
        with(watBinding.root) {
            surface = Surface(renderer.watermarkSurfaceTexture)
            val widthMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(binding.rsv.width, View.MeasureSpec.EXACTLY)
            val heightMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(binding.rsv.height, View.MeasureSpec.EXACTLY)
            this.widthMeasureSpec = widthMeasureSpec
            this.heightMeasureSpec = heightMeasureSpec
            update()
        }
    }

    @SuppressLint("MissingPermission")
    private fun openCamera() = try {
        if (!cameraOpenCloseLock.tryAcquire(2500, TimeUnit.MILLISECONDS)) {
            throw RuntimeException("Time out waiting to lock camera opening.")
        }

        val cameraManager = getSystemService(Context.CAMERA_SERVICE) as CameraManager
        val cameraId = cameraManager.cameraIdList.first {
            cameraManager.getCameraCharacteristics(it)
                .get(CameraCharacteristics.LENS_FACING) == cameraFacing
        }
        val characteristics = cameraManager.getCameraCharacteristics(cameraId)
        sensorOrientation = characteristics[CameraCharacteristics.SENSOR_ORIENTATION] ?: 0
        val configurationMap =
            characteristics[CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP]
                ?: throw RuntimeException("Cannot get available preview/video sizes")
        val previewSize = choosePreviewSize(
            configurationMap.getOutputSizes(SurfaceTexture::class.java),
            resolution.size.width,
            resolution.size.height
        )
        renderer.setupCameraSurfaceTexture(previewSize.width, previewSize.height)
        renderer.cameraSurfaceTexture?.setOnFrameAvailableListener(binding.rsv.renderHandler)

        val rsvW = binding.rsv.width
        val rsvH = binding.rsv.height
        val screenAsp = if (rsvW < rsvH) rsvW * 1.0f / rsvH else rsvH * 1.0f / rsvW
        val previewAsp = previewSize.height * 1.0f / previewSize.width
        val screenToPreviewAsp = if (screenAsp < previewAsp) screenAsp / previewAsp
        else previewAsp / screenAsp
        val w = min(rsvW, rsvH) / min(previewSize.width, previewSize.height).toFloat()
        val h = max(rsvW, rsvH) / max(previewSize.width, previewSize.height).toFloat()
        renderer.transformWidth = screenToPreviewAsp
        renderer.transformHeight = h * (screenToPreviewAsp / w)

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

    @Suppress("DEPRECATION")
    private fun startPreview() {
        try {
            closePreviewSession()

            captureRequestBuilder =
                cameraDevice!!.createCaptureRequest(CameraDevice.TEMPLATE_RECORD)

            val surface = Surface(renderer.cameraSurfaceTexture)
            captureRequestBuilder.addTarget(surface)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                val config = SessionConfiguration(
                    SessionConfiguration.SESSION_REGULAR,
                    listOf(OutputConfiguration(surface)),
                    Executors.newSingleThreadExecutor(),
                    captureSessionCallback
                )
                cameraDevice?.createCaptureSession(config)
            } else {
                cameraDevice?.createCaptureSession(
                    listOf(surface),
                    captureSessionCallback,
                    backgroundHandler
                )
            }
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
        if (recording && binding.rsv.stopRecording()) {
            stopTimer()
            btn.text = "Start"
            recording = false
        } else {
//            val orientationHint = when (sensorOrientation) {
//                SENSOR_ORIENTATION_DEFAULT_DEGREES -> screenRotation
//                SENSOR_ORIENTATION_INVERSE_DEGREES -> sensorOrientation - screenRotation
//                else -> 0
//            }
            try {
                val (videoWidth, videoHeight) =
                    if (binding.rsv.width < binding.rsv.height) resolution.size.height to resolution.size.width
                    else resolution.size.width to resolution.size.height
                binding.rsv.initRecorder(
                    File(videoFilePath),
                    videoWidth,
                    videoHeight,
                    screenRotation
                )
            } catch (e: Exception) {
                Log.e(TAG, "Couldn't re-init recording", e)
            }
            if (binding.rsv.startRecording()) {
                startTimer()
                btn.text = "Stop"
                recording = true
            }
        }
    }

    private fun startTimer() {
        var i = 0
        timer = fixedRateTimer(period = 1000) {
            with(watBinding) {
                txt.text = "${++i}"
                // TODO: animate it
                img.x = Random.nextDouble(0.0, root.width.toDouble()).toFloat()
                img.y = Random.nextDouble(0.0, root.height.toDouble()).toFloat()
                root.update()
            }
        }
    }

    private fun stopTimer() = timer.cancel()

    @SuppressLint("SetTextI18n")
    private fun changeCamera(v: View) {
        v as MaterialButton
        cameraFacing = if (cameraFacing == CameraCharacteristics.LENS_FACING_BACK) {
            binding.btnChangeCamera.text = "back"
            CameraCharacteristics.LENS_FACING_FRONT
        } else {
            binding.btnChangeCamera.text = "front"
            CameraCharacteristics.LENS_FACING_BACK
        }
        renderer.releaseCameraSurfaceTexture()
        closeCamera()
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
            it.width >= width && it.height >= height
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

    override fun onRendererFinished() = Unit

    private companion object {
        val TAG: String = GlCameraActivity::class.java.simpleName

        const val SENSOR_ORIENTATION_DEFAULT_DEGREES = 90
        const val SENSOR_ORIENTATION_INVERSE_DEGREES = 270
    }
}