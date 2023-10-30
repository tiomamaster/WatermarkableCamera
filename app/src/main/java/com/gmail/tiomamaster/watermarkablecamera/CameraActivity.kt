package com.gmail.tiomamaster.watermarkablecamera

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.SurfaceTexture
import android.hardware.camera2.*
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.util.Size
import android.view.*
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.AppCompatImageView
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.android.material.button.MaterialButton
import java.io.File
import java.lang.Long.signum
import java.util.*
import java.util.concurrent.Executors
import java.util.concurrent.Semaphore
import java.util.concurrent.TimeUnit
import kotlin.concurrent.fixedRateTimer
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.random.Random

class CameraActivity : AppCompatActivity(), Renderer.StateListener {

    private lateinit var renderer: Renderer

    private lateinit var watermark: WatermarkView
    private lateinit var watermarkText: AppCompatTextView
    private lateinit var watermarkImage: AppCompatImageView
    private lateinit var rsv: RecordableSurfaceView
    private lateinit var btnStartStop: MaterialButton
    private lateinit var btnChangeCamera: MaterialButton
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
        rsv = findViewById(R.id.rsv)
        btnStartStop = findViewById(R.id.btnStartStop)
        btnChangeCamera = findViewById(R.id.btnChangeCamera)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, rsv).apply {
            hide(WindowInsetsCompat.Type.statusBars() or WindowInsetsCompat.Type.navigationBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        // adjust video's preview size to make it aspect ratio equal to recorded video
        val height = resources.displayMetrics.heightPixels
        val width = resources.displayMetrics.widthPixels
        if (height > width) {
            rsv.layoutParams.height = (width * ASP).roundToInt()
        } else {
            rsv.layoutParams.width = (height * ASP).roundToInt()
        }

        renderer = Renderer(applicationContext, this)
        rsv.rendererCallbacks = renderer
    }

    override fun onResume() {
        super.onResume()
        startBackgroundThread()
        rsv.resume()
        orientationEventListener.enable()
    }

    override fun onPause() {
        super.onPause()
        closeCamera()
        stopBackgroundThread()
        renderer.releaseSurfaceTextures()
        rsv.pause()
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

    @SuppressLint("InflateParams", "Recycle")
    private fun setupWatermark() {
        renderer.setupWatermarkSurfaceTexture(rsv.width, rsv.height)
        watermark = layoutInflater.inflate(R.layout.watermark, null) as WatermarkView
        watermarkText = watermark.findViewById(R.id.text)
        watermarkImage = watermark.findViewById(R.id.img)
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
            update()
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
                rsv.width,
                rsv.height
            )
            renderer.setupCameraSurfaceTexture(previewSize.width, previewSize.height)

            val screenAsp = if (rsv.width < rsv.height) rsv.width * 1.0f / rsv.height
            else rsv.height * 1.0f / rsv.width
            val previewAsp = previewSize.height * 1.0f / previewSize.width
            renderer.screenToPreviewAsp = if (screenAsp < previewAsp) screenAsp / previewAsp
            else previewAsp / screenAsp

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

    @Suppress("DEPRECATION")
    private fun startPreview() {
        try {
            closePreviewSession()

            captureRequestBuilder =
                cameraDevice!!.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)

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
        if (recording && rsv.stopRecording()) {
            stopTimer()
            btn.text = "Start"
            recording = false
        } else {
            val orientationHint = when (sensorOrientation) {
                SENSOR_ORIENTATION_DEFAULT_DEGREES -> screenRotation
                SENSOR_ORIENTATION_INVERSE_DEGREES -> sensorOrientation - screenRotation
                else -> 0
            }
            try {
                val (videoWidth, videoHeight) = if (rsv.width < rsv.height) WIDTH to HEIGHT else HEIGHT to WIDTH
                rsv.initRecorder(
                    File(videoFilePath),
                    videoWidth,
                    videoHeight,
                    orientationHint,
                    null,
                    null
                )
            } catch (e: Exception) {
                Log.e(TAG, "Couldn't re-init recording", e)
            }
            if (rsv.startRecording()) {
                startTimer()
                btn.text = "Stop"
                recording = true
            }
        }
    }

    private fun startTimer() {
        var i = 0
        timer = fixedRateTimer(period = 1000) {
            watermarkText.text = "${i++}"
            // TODO: animate it
            watermarkImage.x = Random.nextDouble(0.0, watermark.width.toDouble()).toFloat()
            watermarkImage.y = Random.nextDouble(0.0, watermark.height.toDouble()).toFloat()
            watermark.update()
        }
    }

    private fun stopTimer() = timer.cancel()

    @SuppressLint("SetTextI18n")
    private fun changeCamera(v: View) {
        v as MaterialButton
        cameraFacing = if (cameraFacing == CameraCharacteristics.LENS_FACING_BACK) {
            btnChangeCamera.text = "back"
            CameraCharacteristics.LENS_FACING_FRONT
        } else {
            btnChangeCamera.text = "front"
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

        val TAG: String = CameraActivity::class.java.simpleName

        val PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        const val REQUEST_CODE_PERMISSIONS = 0xCA

        const val SENSOR_ORIENTATION_DEFAULT_DEGREES = 90
        const val SENSOR_ORIENTATION_INVERSE_DEGREES = 270

        const val WIDTH = 720
        const val HEIGHT = 1280
        const val ASP = HEIGHT / WIDTH.toFloat()
    }
}