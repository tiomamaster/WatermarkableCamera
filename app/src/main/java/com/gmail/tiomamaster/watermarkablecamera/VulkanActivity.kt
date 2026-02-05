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
import android.widget.FrameLayout
import androidx.appcompat.widget.AppCompatImageView
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.androidgamesdk.GameActivity
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.system.exitProcess

class VulkanActivity : GameActivity() {

    private lateinit var watermark: WatermarkView
    private lateinit var watermarkText: AppCompatTextView
    private lateinit var watermarkImage: AppCompatImageView

    private val mediaSurface = MediaCodec.createPersistentInputSurface()
    private lateinit var mediaRecorder: MediaRecorder
    private var recording = AtomicBoolean(false)

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
        watermark = layoutInflater.inflate(R.layout.watermark, null) as WatermarkView
        watermarkText = watermark.findViewById(R.id.text)
        watermarkImage = watermark.findViewById(R.id.img)
        with(watermark) {
            val w = windowManager.currentWindowMetrics.bounds.width()
            val h = windowManager.currentWindowMetrics.bounds.height()
            val widthMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY)
            val heightMeasureSpec =
                View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY)
            this.widthMeasureSpec = widthMeasureSpec
            this.heightMeasureSpec = heightMeasureSpec
            Log.i(TAG, "Setup watermark with size $w:$h")

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
            setAudioSource(MediaRecorder.AudioSource.MIC)
            setOutputFormat(OutputFormat.MPEG_4)

            setAudioEncoder(MediaRecorder.AudioEncoder.AAC)
            setAudioEncodingBitRate(16)
            setAudioSamplingRate(44100)

            setVideoEncoder(MediaRecorder.VideoEncoder.H264)

            setVideoEncodingBitRate(10000000)
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
        mediaRecorder.start()
        recording.set(true)
        true
    }.onFailure {
        recording.set(false)
        mediaRecorder.reset()
        mediaSurface.release()
    }.getOrNull() ?: false

    fun stopRecording(): Boolean = if (recording.get()) {
        kotlin.runCatching {
            mediaRecorder.stop()
            recording.set(false)
            true
        }.onFailure {
            mediaRecorder.release()
        }.getOrNull() ?: false
    } else {
        throw IllegalStateException("Cannot stop. Is not recording.")
    }

    override fun onCreateSurfaceView() {
        mSurfaceView = createSurfaceView() ?: return

        setContentView(R.layout.activity_vulkan)
        val frameLayout: FrameLayout = findViewById(R.id.frameLayout)
        frameLayout.addView(mSurfaceView)

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

    private companion object {
        init {
            System.loadLibrary("WatermarkableCameraJNI")
        }

        val TAG: String = VulkanActivity::class.java.simpleName
    }
}