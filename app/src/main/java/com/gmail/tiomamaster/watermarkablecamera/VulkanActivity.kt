package com.gmail.tiomamaster.watermarkablecamera

import android.annotation.SuppressLint
import android.hardware.HardwareBuffer
import android.os.Build.VERSION
import android.os.Build.VERSION_CODES
import android.os.Bundle
import android.os.Handler
import android.util.Log
import android.view.KeyEvent
import android.view.Surface
import android.view.View
import android.view.WindowManager.LayoutParams
import androidx.appcompat.widget.AppCompatImageView
import androidx.appcompat.widget.AppCompatTextView
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.google.androidgamesdk.GameActivity
import kotlin.concurrent.fixedRateTimer
import kotlin.random.Random

class VulkanActivity : GameActivity() {

    private lateinit var watermark: WatermarkView
    private lateinit var watermarkText: AppCompatTextView
    private lateinit var watermarkImage: AppCompatImageView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        hideSystemUI()

        Handler(mainLooper).postDelayed({
            setupWatermark()
        }, 1000)

        var i = 0
        fixedRateTimer(period = 1000, initialDelay = 1500) {
            watermarkText.text = "${++i}"
            // TODO: animate it
            watermarkImage.x = Random.nextDouble(0.0, watermark.width.toDouble()).toFloat()
            watermarkImage.y = Random.nextDouble(0.0, watermark.height.toDouble()).toFloat()
            watermark.update()
        }
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
        System.exit(0)
    }

    private external fun test(hwBuff: HardwareBuffer)
    private external fun getWatermarkSurface(): Surface

    private companion object {
        init {
            System.loadLibrary("WatermarkableCameraJNI")
        }

        val TAG: String = VulkanActivity::class.java.simpleName
    }
}