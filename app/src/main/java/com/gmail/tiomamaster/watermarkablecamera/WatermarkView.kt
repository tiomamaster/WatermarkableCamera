package com.gmail.tiomamaster.watermarkablecamera

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.PorterDuff
import android.util.AttributeSet
import android.view.Surface
import androidx.constraintlayout.widget.ConstraintLayout

class WatermarkView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : ConstraintLayout(context, attrs, defStyleAttr) {

    var surface: Surface? = null

    var widthMeasureSpec: Int = 0
    var heightMeasureSpec: Int = 0

    fun update() = draw(null)

    override fun draw(canvas: Canvas?) {
        if (surface == null) return
        try {
            val surfaceCanvas = surface?.lockCanvas(null)
            surfaceCanvas?.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)

            measure(widthMeasureSpec, heightMeasureSpec)
            layout(0, 0, measuredWidth, measuredHeight)

            super.draw(surfaceCanvas)
            surface?.unlockCanvasAndPost(surfaceCanvas)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }
}