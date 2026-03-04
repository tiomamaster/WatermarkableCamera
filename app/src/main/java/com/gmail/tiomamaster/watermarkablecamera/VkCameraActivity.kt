package com.gmail.tiomamaster.watermarkablecamera

import android.annotation.SuppressLint
import android.hardware.HardwareBuffer
import android.media.MediaCodec
import android.media.MediaRecorder
import android.media.MediaRecorder.OutputFormat
import android.os.Build.VERSION
import android.os.Build.VERSION_CODES
import android.os.Bundle
import android.os.Handler
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager.LayoutParams
import androidx.core.view.ViewCompat
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        hideSystemUI()

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
//            // TODO: animate it
//            watermarkImage.x = Random.nextDouble(0.0, watermark.width.toDouble()).toFloat()
//            watermarkImage.y = Random.nextDouble(0.0, watermark.height.toDouble()).toFloat()
//            watermark.update()
//        }
    }

    @SuppressLint("InflateParams", "Recycle")
    private fun setupWatermark() {
        watBinding = WatermarkBinding.inflate(layoutInflater)
        with(watBinding.root) {
//            val width = resources.displayMetrics.widthPixels
//            val height = resources.displayMetrics.heightPixels
            val width = mSurfaceView.width
            val height = mSurfaceView.height
            val widthMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY)
            val heightMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY)
            this.widthMeasureSpec = widthMeasureSpec
            this.heightMeasureSpec = heightMeasureSpec
            Log.i(TAG, "Setup watermark with size $width:$height")

//            val watImageReader = ImageReader.newInstance(
//                w,
//                h,
//                PixelFormat.RGBA_8888,
//                2,
//                HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE
//            )

//            surface = watImageReader.surface
            surface = getWatermarkSurface()

//            watImageReader.setOnImageAvailableListener({
//                Log.i(TAG, "new image available")
//                val image = it.acquireLatestImage()
//                val hwBuff = image.hardwareBuffer
//                // process hwBuff, like render image using vk
//                Log.i(
//                    TAG,
//                    "hardware buffer acquired, ${hwBuff?.width}:${hwBuff?.height}:${hwBuff?.format}"
//                )
//                test(hwBuff!!)
//                hwBuff.close()
//                image.close()
//            }, null)
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

    private fun hideSystemUI() {
        // This will put the game behind any cutouts and waterfalls on devices which have
        // them, so the corresponding insets will be non-zero.

        // We cannot guarantee that AndroidManifest won't be tweaked
        // and we don't want to crash if that happens so we suppress warning.
        @SuppressLint("ObsoleteSdkInt")
        if (VERSION.SDK_INT >= VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        }
        val decorView: View = window.decorView
        val controller = WindowInsetsControllerCompat(
            window,
            decorView
        )
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.hide(WindowInsetsCompat.Type.displayCutout())
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
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
        if (height > width) {
            binding.frameLayout.layoutParams.height = (width * ASP).roundToInt()
        } else {
            binding.frameLayout.layoutParams.width = (height * ASP).roundToInt()
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

    private external fun test(hwBuff: HardwareBuffer)
    private external fun getWatermarkSurface(): Surface
    private external fun setMediaSurface(surface: Surface)
    private external fun nativeStartStopRecording()

    private companion object {
        init {
            System.loadLibrary("WatermarkableCameraJNI")
        }

        val TAG: String = VkCameraActivity::class.java.simpleName

        const val WIDTH = 720
        const val HEIGHT = 1280
        const val ASP = HEIGHT / WIDTH.toFloat()
    }
}