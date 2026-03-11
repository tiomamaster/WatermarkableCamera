package com.gmail.tiomamaster.watermarkablecamera

import android.media.MediaCodec
import android.media.MediaRecorder
import android.media.MediaRecorder.OutputFormat
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.gmail.tiomamaster.watermarkablecamera.databinding.ActivityCameraVkBinding
import com.gmail.tiomamaster.watermarkablecamera.databinding.WatermarkBinding
import com.google.androidgamesdk.GameActivity
import java.io.File
import kotlin.math.roundToInt
import kotlin.system.exitProcess

class VkCameraActivity : GameActivity() {

    private lateinit var binding: ActivityCameraVkBinding
    private lateinit var watBinding: WatermarkBinding

    private val mediaSurface = MediaCodec.createPersistentInputSurface()
    private lateinit var mediaRecorder: MediaRecorder
    private var recording = false

    private var resolution = Resolution.FHD

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "Called onCreate")

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

        resolution = Resolution.entries[intent.getIntExtra(MainActivity.EXTRA_RESOLUTION, 1)]

        Handler(mainLooper).postDelayed({
            setupWatermark()
            val filename = "${System.currentTimeMillis()}.mp4"
            val dir = /*getExternalFilesDir(null)*/"/sdcard/DCIM/Camera"
            initRecorder(File("$dir/$filename"), 720, 1280, 0)
            setMediaSurface(mediaSurface)
        }, 1000)

//        var i = 0
//        fixedRateTimer(period = 1000, initialDelay = 1500) {
//            watermarkText.text = "${++i}"
//            watermarkImage.x = Random.nextDouble(0.0, watermark.width.toDouble()).toFloat()
//            watermarkImage.y = Random.nextDouble(0.0, watermark.height.toDouble()).toFloat()
//            watermark.update()
//        }
    }

    override fun onStart() {
        super.onStart()
        Log.i(TAG, "Called onStart")
    }

    override fun onResume() {
        super.onResume()
        Log.i(TAG, "Called onResume")
    }

    private fun setupWatermark() {
        watBinding = WatermarkBinding.inflate(layoutInflater)
        with(watBinding.root) {
            surface = getWatermarkSurface()
            val widthMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(mSurfaceView.width, View.MeasureSpec.EXACTLY)
            val heightMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(mSurfaceView.height, View.MeasureSpec.EXACTLY)
            this.widthMeasureSpec = widthMeasureSpec
            this.heightMeasureSpec = heightMeasureSpec
            update()
        }
    }

    private fun initRecorder(
        saveTo: File,
        desiredWidth: Int,
        desiredHeight: Int,
        orientationHint: Int
    ) {
        mediaRecorder = MediaRecorder(this).apply {
            setVideoSource(MediaRecorder.VideoSource.SURFACE)
            setInputSurface(mediaSurface)
            setAudioSource(MediaRecorder.AudioSource.CAMCORDER)
            setOutputFormat(OutputFormat.MPEG_4)

            setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            setAudioEncodingBitRate(10_000_000)
            setAudioSamplingRate(96000)
            setAudioChannels(2)

            setVideoEncoder(MediaRecorder.VideoEncoder.H264)
            setVideoEncodingBitRate(10_000_000)
            setVideoFrameRate(30)
            setVideoSize(desiredWidth, desiredHeight)

            setOrientationHint(orientationHint)

            setOutputFile(saveTo.absolutePath)
            prepare()
        }
    }

    fun startRecording(): Boolean = kotlin.runCatching {
        nativeStartStopRecording()
        mediaRecorder.start()
        recording = true
        true
    }.onFailure {
        recording = false
        mediaRecorder.reset()
        mediaSurface.release()
    }.getOrNull() ?: false

    fun stopRecording(): Boolean = if (recording) {
        kotlin.runCatching {
            nativeStartStopRecording()
            mediaRecorder.stop()
            recording = false
            true
        }.onFailure {
            mediaRecorder.release()
        }.getOrNull() ?: false
    } else {
        throw IllegalStateException("Cannot stop. Is not recording.")
    }

    override fun onCreateSurfaceView() {
        Log.i(TAG, "Called onCreateSurfaceView")

        mSurfaceView = createSurfaceView() ?: return

        binding = ActivityCameraVkBinding.inflate(layoutInflater)
        setContentView(binding.root)
        binding.frameLayout.addView(mSurfaceView)

        binding.btnStartStop.setOnClickListener {
            if (recording) stopRecording() else startRecording()
        }

        // adjust video's preview size to make it aspect ratio equal to recorded video
        val height = resources.displayMetrics.heightPixels
        val width = resources.displayMetrics.widthPixels
        val asp = resolution.size.width / resolution.size.height.toFloat()
        if (height > width) {
            binding.frameLayout.layoutParams.height = (width * asp).roundToInt()
        } else {
            binding.frameLayout.layoutParams.width = (height * asp).roundToInt()
        }

        // Register as a callback for the rendering of the surface, so that we can pass this
        // surface to the native code
        mSurfaceView.holder.addCallback(this)

        // Note that in order for system window inset changes to be useful, the activity must call
        // WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        // Otherwise, the view will always be inside any system windows.
        // Listen for insets changes
        ViewCompat.setOnApplyWindowInsetsListener(mSurfaceView, this)
    }

    // Filter out back button press, and handle it here after native
    // side done its processing. Application can also make a reverse JNI
    // call to onBackPressed()/finish() at the end of the KEYCODE_BACK
    // processing.
    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        var processed = super.onKeyDown(keyCode, event);
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            onBackPressed()
            processed = true
        }
        return processed
    }

    // TODO: Migrate to androidx.activity.OnBackPressedCallback.
    // onBackPressed is deprecated.
    override fun onBackPressed() {
        System.gc()
        exitProcess(0)
    }

    private external fun getWatermarkSurface(): Surface
    private external fun setMediaSurface(surface: Surface)
    private external fun nativeStartStopRecording()

    private companion object {
        init {
            System.loadLibrary("WatermarkableCameraJNI")
        }

        val TAG: String = VkCameraActivity::class.java.simpleName
    }
}