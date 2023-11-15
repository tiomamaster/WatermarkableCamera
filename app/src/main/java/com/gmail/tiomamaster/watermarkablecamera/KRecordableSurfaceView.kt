package com.gmail.tiomamaster.watermarkablecamera

import android.content.Context
import android.graphics.SurfaceTexture
import android.media.MediaCodec
import android.media.MediaRecorder
import android.media.MediaRecorder.OutputFormat
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.opengl.GLES20
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Message
import android.util.AttributeSet
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

class KRecordableSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0
) : SurfaceView(context, attrs, defStyle) {

    lateinit var renderer: Renderer

    val renderHandler by lazy {
        val renderThread = HandlerThread("RenderThread").apply { start() }
        RenderHandler(renderThread.looper)
    }

    private val mediaSurface = MediaCodec.createPersistentInputSurface()
    private lateinit var mediaRecorder: MediaRecorder
    private var recording = AtomicBoolean(false)
    private var width = 0
    private var height = 0
    private var desiredWidth = 0
    private var desiredHeight = 0

    init {
        holder.addCallback(renderHandler)
    }

    fun initRecorder(saveTo: File, desiredWidth: Int, desiredHeight: Int, orientationHint: Int) {
        mediaRecorder = MediaRecorder(context).apply {
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

            this@KRecordableSurfaceView.desiredWidth = desiredWidth
            this@KRecordableSurfaceView.desiredHeight = desiredHeight

            setVideoSize(desiredWidth, desiredHeight)

            setOrientationHint(orientationHint)

            setOutputFile(saveTo.absolutePath)
            prepare()
        }
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


    inner class RenderHandler(looper: Looper) : Handler(looper),
        SurfaceTexture.OnFrameAvailableListener, SurfaceHolder.Callback2 {

        private var eglDisplay = EGL14.EGL_NO_DISPLAY
        private var eglContext = EGL14.EGL_NO_CONTEXT
        private var eglConfig: EGLConfig? = null
        private var eglSurface: EGLSurface = EGL14.EGL_NO_SURFACE
        private var eglSurfaceMedia: EGLSurface = EGL14.EGL_NO_SURFACE
        private val configAttribList = intArrayOf(
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            0x3142, 1,
            EGL14.EGL_DEPTH_SIZE, 16,
            EGL14.EGL_NONE
        )

        init {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                configAttribList[10] = EGLExt.EGL_RECORDABLE_ANDROID
            }
        }

        private fun createResources(surface: Surface) {
            if (eglContext == EGL14.EGL_NO_CONTEXT) {
                initEGL(surface)
            }
        }

        private fun initEGL(surface: Surface) {
            eglDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
            val version = intArrayOf(0, 0)
            EGL14.eglInitialize(eglDisplay, version, 0, version, 1)
            val configs = arrayOfNulls<EGLConfig>(1)
            val numConfigs = intArrayOf(1)
            EGL14.eglChooseConfig(
                eglDisplay,
                configAttribList,
                0,
                configs,
                0,
                configs.size,
                numConfigs,
                0
            )
            eglConfig = configs[0]!!
            val contextAttribList = intArrayOf(
                EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL14.EGL_NONE
            )
            eglContext = EGL14.eglCreateContext(
                eglDisplay,
                eglConfig,
                EGL14.EGL_NO_CONTEXT,
                contextAttribList,
                0
            )
            val surfaceAttrs = intArrayOf(EGL14.EGL_NONE)
            eglSurface = EGL14.eglCreateWindowSurface(
                eglDisplay,
                eglConfig,
                surface,
                surfaceAttrs,
                0
            )
            EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)
            renderer.onSurfaceCreated()

            eglSurfaceMedia = EGL14.eglCreateWindowSurface(
                eglDisplay,
                eglConfig,
                mediaSurface,
                surfaceAttrs,
                0
            )
            GLES20.glClearColor(0.1f, 0.1f, 0.1f, 1.0f)
            renderer.onContextCreated()
        }

        private fun onFrameAvailableImpl(surfaceTexture: SurfaceTexture) {
            if (eglContext == EGL14.EGL_NO_CONTEXT) return

            surfaceTexture.updateTexImage()

            renderer.onDrawFrame()
            EGL14.eglSwapBuffers(eglDisplay, eglSurface)

            if (!recording.get()) return
            EGL14.eglMakeCurrent(eglDisplay, eglSurfaceMedia, eglSurfaceMedia, eglContext)
            GLES20.glViewport(0, 0, desiredWidth, desiredHeight)
            renderer.onDrawFrame()
            GLES20.glViewport(0, 0, width, height)
            EGL14.eglSwapBuffers(eglDisplay, eglSurfaceMedia)
            EGL14.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext)
        }

        override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
            sendMessage(obtainMessage(MSG_ON_FRAME_AVAILABLE, 0, 0, surfaceTexture))
        }

        override fun surfaceCreated(holder: SurfaceHolder) {
            sendMessage(obtainMessage(MSG_CREATE_RESOURCES, 0, 0, holder.surface))
        }

        private fun surfaceChangedImpl() {
            GLES20.glViewport(0, 0, width, height)
        }

        override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
            this@KRecordableSurfaceView.width = width
            this@KRecordableSurfaceView.height = height
            sendMessage(obtainMessage(MSG_SURFACE_CHANGED, 0, 0, null))
        }

        override fun surfaceDestroyed(holder: SurfaceHolder) {
            renderer.onSurfaceDestroyed()
            holder.removeCallback(this)
        }

        override fun surfaceRedrawNeeded(holder: SurfaceHolder) {
        }

        override fun handleMessage(msg: Message) {
            when (msg.what) {
                MSG_CREATE_RESOURCES -> createResources(msg.obj as Surface)
                MSG_SURFACE_CHANGED -> surfaceChangedImpl()
                MSG_ON_FRAME_AVAILABLE -> onFrameAvailableImpl(msg.obj as SurfaceTexture)
            }
        }
    }

    companion object {
        private const val MSG_CREATE_RESOURCES = 0
        private const val MSG_SURFACE_CHANGED = 1
        private const val MSG_ON_FRAME_AVAILABLE = 2
    }
}
