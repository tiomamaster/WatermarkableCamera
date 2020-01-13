package com.gmail.tiomamaster.watermarkablecamera

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLUtils
import android.opengl.Matrix
import android.util.Log
import java.io.InputStreamReader
import java.lang.ref.WeakReference
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.ShortBuffer
import android.opengl.GLES20 as gl2

class Renderer(
    private val context: Context,
    listener: StateListener
) : RecordableSurfaceView.RendererCallbacks {

    private val listener = WeakReference(listener)

    var ready = false

    var screenToPreviewAsp = 1f
    var rotation = 0f

    var cameraSurfaceTexture: SurfaceTexture? = null
    private var camFrameAvailableRegistered = false
    private var cameraRefreshCount = 0
    private val cameraTransformMatrix = FloatArray(16)

    var watermarkSurfaceTexture: SurfaceTexture? = null
    private var watFrameAvailableRegistered = false
    private var needUpdateWatermarkTexture = false
    private val watermarkTransformMatrix = FloatArray(16)

    private val textureIds = IntArray(2)

    private var program = 0

    private var positionHandle = 0
    private var textureCoordinateHandle = 0
    private var positionMatrixHandle = 0
    private var textureTransformHandle = 0
    private var textureHandle = 0

    @SuppressLint("Recycle")
    fun setupCameraSurfaceTextures(width: Int, height: Int) {
        cameraSurfaceTexture = SurfaceTexture(textureIds[0]).apply {
            setDefaultBufferSize(width, height)
        }
    }

    @SuppressLint("Recycle")
    fun setupWatermarkSurfaceTexture(width: Int, height: Int) {
        watermarkSurfaceTexture = SurfaceTexture(textureIds[1]).apply {
            setDefaultBufferSize(width, height)
        }
    }

    fun releaseSurfaceTextures() {
        releaseCameraSurfaceTexture()
        watermarkSurfaceTexture?.release()
        watermarkSurfaceTexture = null
        watFrameAvailableRegistered = false
    }

    fun releaseCameraSurfaceTexture() {
        cameraSurfaceTexture?.release()
        cameraSurfaceTexture = null
        camFrameAvailableRegistered = false
    }

    override fun onSurfaceCreated() {
        initGlComponents()
        ready = true
    }

    override fun onSurfaceChanged(width: Int, height: Int) {
        // TODO: ???
    }

    override fun onSurfaceDestroyed() {
        cleanupGlComponents()
        ready = false
    }

    private fun initGlComponents() {
        setupTextures()
        createProgram()

        listener.get()?.onRendererReady()
    }

    private fun setupTextures() {
        withGlErrorChecking("Texture generate") {
            gl2.glGenTextures(
                textureIds.size,
                textureIds,
                0
            )
        }
    }

    private fun createProgram() {
        fun compileShader(type: Int): Int {
            val (path, operation) =
                if (type == gl2.GL_VERTEX_SHADER) {
                    VERTEX_SHADER_PATH to "Vertex shader compile"
                } else {
                    FRAGMENT_SHADER_PATH to "Fragment shader compile"
                }
            val shaderCode =
                context.assets.open(path).reader().use(InputStreamReader::readText)
            val shaderHandle = gl2.glCreateShader(type)
            gl2.glShaderSource(shaderHandle, shaderCode)
            withGlErrorChecking(operation) {
                gl2.glCompileShader(shaderHandle)
            }
            return shaderHandle
        }

        val vertexShaderHandle = compileShader(gl2.GL_VERTEX_SHADER)
        val fragmentShaderHandle = compileShader(gl2.GL_FRAGMENT_SHADER)
        program = gl2.glCreateProgram()
        gl2.glAttachShader(program, vertexShaderHandle)
        gl2.glAttachShader(program, fragmentShaderHandle)
        withGlErrorChecking("Shader program link") {
            gl2.glLinkProgram(program)
        }
        verifyProgram()
        gl2.glUseProgram(program)
        setupHandlers()
    }

    private fun verifyProgram() {
        if (!BuildConfig.DEBUG) return
        with(IntArray(1)) {
            gl2.glGetProgramiv(program, gl2.GL_LINK_STATUS, this, 0)
            if (get(0) != gl2.GL_TRUE) {
                Log.e(TAG, "Error while linking a program:\n${gl2.glGetProgramInfoLog(program)}")
            }
        }
    }

    private fun setupHandlers() {
        positionHandle = gl2.glGetAttribLocation(program, "aPosition")
        textureCoordinateHandle = gl2.glGetAttribLocation(program, "aTexCoordinate")
        positionMatrixHandle = gl2.glGetUniformLocation(program, "uMVPMatrix")
        textureTransformHandle = gl2.glGetUniformLocation(program, "uTextureTransform")
        textureHandle = gl2.glGetUniformLocation(program, "sTexture")
    }

    private fun cleanupGlComponents() {
        withGlErrorChecking("Delete textures") {
            gl2.glDeleteTextures(
                textureIds.size,
                textureIds,
                0
            )
        }
        withGlErrorChecking("Delete program") { gl2.glDeleteProgram(program) }
    }

    override fun onContextCreated() = Unit

    override fun onPreDrawFrame() {
        if (!camFrameAvailableRegistered) {
            cameraSurfaceTexture?.apply {
                setOnFrameAvailableListener {
                    getTransformMatrix(cameraTransformMatrix)
                    cameraRefreshCount++
                }
                camFrameAvailableRegistered = true
            }
        }
        if (!watFrameAvailableRegistered) {
            watermarkSurfaceTexture?.apply {
                setOnFrameAvailableListener {
                    needUpdateWatermarkTexture = true
                }
                watFrameAvailableRegistered = true
            }
        }
    }

    override fun onDrawFrame() {
        gl2.glEnable(gl2.GL_BLEND)
        gl2.glBlendFunc(gl2.GL_ONE, gl2.GL_ONE_MINUS_SRC_ALPHA)

        val orthoMatrix = FloatArray(16)
        Matrix.orthoM(
            orthoMatrix,
            0,
            -screenToPreviewAsp,
            screenToPreviewAsp,
            -1.0f,
            1.0f,
            -1f,
            1f
        )
        val rotateMatrix = FloatArray(16).apply {
            Matrix.setIdentityM(this, 0)
            Matrix.rotateM(this, 0, rotation, 0f, 0f, 1f)
        }
        val mvpMatrix = FloatArray(16)
        Matrix.multiplyMM(mvpMatrix, 0, rotateMatrix, 0, orthoMatrix, 0)
        drawCamera(mvpMatrix)

        Matrix.orthoM(
            mvpMatrix,
            0,
            -1f,
            1f,
            -1f,
            1f,
            -1f,
            1f
        )
        drawWatermark(mvpMatrix)
    }

    private fun drawCamera(mvpMatrix: FloatArray) {
        // call updateTexImage() exactly the same count as onFrameAvailable listener was triggered
        // onFrameAvailable won't trigger if skip some updates
        while (cameraRefreshCount > 0) {
            cameraSurfaceTexture?.updateTexImage()
            cameraRefreshCount--
        }

        gl2.glUniformMatrix4fv(textureTransformHandle, 1, false, cameraTransformMatrix, 0)

        gl2.glActiveTexture(gl2.GL_TEXTURE0)
        withGlErrorChecking("Bind camera texture") {
            gl2.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureIds[0])
        }
        gl2.glUniform1i(textureHandle, 0)

        draw(mvpMatrix)
    }

    private fun drawWatermark(mvpMatrix: FloatArray) {
        if (needUpdateWatermarkTexture) watermarkSurfaceTexture?.updateTexImage()
        watermarkSurfaceTexture?.getTransformMatrix(watermarkTransformMatrix)

        gl2.glUniformMatrix4fv(textureTransformHandle, 1, false, watermarkTransformMatrix, 0)

        gl2.glActiveTexture(gl2.GL_TEXTURE1)
        withGlErrorChecking("Bind watermark texture") {
            gl2.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureIds[1])
        }
        gl2.glUniform1i(textureHandle, 1)

        draw(mvpMatrix)
    }

    private fun draw(mvpMatrix: FloatArray) {
        gl2.glEnableVertexAttribArray(positionHandle)
        gl2.glVertexAttribPointer(
            positionHandle,
            2,
            gl2.GL_FLOAT,
            false,
            8,
            vertexCoordinatesBuffer
        )

        gl2.glEnableVertexAttribArray(textureCoordinateHandle)
        gl2.glVertexAttribPointer(
            textureCoordinateHandle,
            2,
            gl2.GL_FLOAT,
            false,
            8,
            textureCoordinatesBuffer
        )

        gl2.glUniformMatrix4fv(positionMatrixHandle, 1, false, mvpMatrix, 0)

        gl2.glDrawElements(gl2.GL_TRIANGLES, 6, gl2.GL_UNSIGNED_SHORT, drawOrderBuffer)

        cleanupDraw()
    }

    private fun cleanupDraw() {
        gl2.glDisableVertexAttribArray(positionHandle)
        gl2.glDisableVertexAttribArray(textureCoordinateHandle)
    }

    private inline fun withGlErrorChecking(operation: String? = null, glBlock: () -> Unit) {
        glBlock()
        val error = gl2.glGetError()
        if (BuildConfig.DEBUG && error != gl2.GL_NO_ERROR) {
            operation?.let { Log.e(TAG, "$it failed") }
            throw RuntimeException(GLUtils.getEGLErrorString(error))
        }
    }

    interface StateListener {
        fun onRendererReady()
        fun onRendererFinished()
    }

    private companion object {

        val TAG = Renderer::class.java.simpleName

        const val VERTEX_SHADER_PATH = "shaders/tex.vert"
        const val FRAGMENT_SHADER_PATH = "shaders/tex.frag"

        fun floatArrayToBuffer(array: FloatArray): FloatBuffer = ByteBuffer
            .allocateDirect(array.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(array).apply { position(0) }

        fun shortArrayToBuffer(array: ShortArray): ShortBuffer = ByteBuffer
            .allocateDirect(array.size * 2)
            .order(ByteOrder.nativeOrder())
            .asShortBuffer()
            .put(array).apply { position(0) }

        const val squareSize = 1.0f
        val vertexCoordinatesBuffer = floatArrayOf(
            -squareSize, squareSize,  // top left
            squareSize, squareSize,   // top right
            -squareSize, -squareSize, // bottom left
            squareSize, -squareSize   // bottom right
        ).run(::floatArrayToBuffer)
        val textureCoordinatesBuffer = floatArrayOf(
            0.0f, 1.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f
        ).run(::floatArrayToBuffer)
        val drawOrderBuffer = shortArrayOf(0, 1, 2, 1, 3, 2).run(::shortArrayToBuffer)
    }
}