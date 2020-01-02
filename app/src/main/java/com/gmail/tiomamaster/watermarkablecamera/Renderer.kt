package com.gmail.tiomamaster.watermarkablecamera

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLUtils
import android.opengl.Matrix
import android.util.Log
import java.io.InputStreamReader
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.ShortBuffer
import android.opengl.GLES20 as gl2

class Renderer(private val context: Context, private val listener: StateListener) :
    RecordableSurfaceView.RendererCallbacks,
    SurfaceTexture.OnFrameAvailableListener {

    override fun onSurfaceCreated() {
        cleanupGlComponents()
        initGlComponents()
    }

    override fun onSurfaceChanged(width: Int, height: Int) {
        // TODO: ???
    }

    override fun onSurfaceDestroyed() {
        cleanupGlComponents()
    }

    override fun onContextCreated() = Unit

    override fun onPreDrawFrame() = Unit

    var screenToPreviewAspectRatio = 1f

    override fun onDrawFrame() {
        gl2.glEnable(gl2.GL_BLEND)
        gl2.glBlendFunc(gl2.GL_ONE, gl2.GL_ONE_MINUS_SRC_ALPHA)
        gl2.glUseProgram(program)

        val mvpMatrix = FloatArray(16)
        Matrix.orthoM(
            mvpMatrix,
            0,
            -screenToPreviewAspectRatio,
            screenToPreviewAspectRatio,
            -1f,
            1f,
            -1f,
            1f
        )
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
        gl2.glUniformMatrix4fv(textureTransformHandle, 1, false, cameraTransformMatrix, 0)

        gl2.glActiveTexture(gl2.GL_TEXTURE0)
        gl2.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureIds[0])
        gl2.glUniform1i(textureHandle, 0)

        draw(mvpMatrix)
    }

    private fun drawWatermark(mvpMatrix: FloatArray) {
        gl2.glUniformMatrix4fv(textureTransformHandle, 1, false, watermarkTransformMatrix, 0)

        gl2.glActiveTexture(gl2.GL_TEXTURE1)
        gl2.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureIds[1])
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

    val cameraSurfaceTexture: SurfaceTexture by lazy(LazyThreadSafetyMode.NONE) {
        SurfaceTexture(
            textureIds[0]
        ).apply {
            setOnFrameAvailableListener(this@Renderer)
        }
    }
    private val cameraTransformMatrix = FloatArray(16)

    val watermarkSurfaceTexture: SurfaceTexture by lazy(LazyThreadSafetyMode.NONE) {
        SurfaceTexture(
            textureIds[0]
        ).apply {
            setOnFrameAvailableListener(this@Renderer)
        }
    }
    private val watermarkTransformMatrix = FloatArray(16)

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        surfaceTexture?.updateTexImage()

        cameraSurfaceTexture.getTransformMatrix(cameraTransformMatrix)
        watermarkSurfaceTexture.getTransformMatrix(watermarkTransformMatrix)
    }

    private fun initGlComponents() {
        setupTextures()
        createProgram()

        listener.onRendererReady()
    }

    private fun cleanupGlComponents() {
        gl2.glDeleteTextures(2, textureIds, 0)
        gl2.glDeleteProgram(program)
    }

    private val textureIds = IntArray(2)

    private fun setupTextures() {
        withGlErrorChecking("Texture generate") {
            gl2.glGenTextures(2, textureIds, 0)
        }

        // TODO: ???
//        setupCameraTexture()
//        setupWatermarkTexture()
    }

//    private fun setupCameraTexture() {
//        withGlErrorChecking("Camera texture bind") {
//            gl2.glActiveTexture(gl2.GL_TEXTURE0)
//            gl2.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureIds[0])
//        }
//    }
//
//    private fun setupWatermarkTexture() {
//        withGlErrorChecking("Watermark texture bind") {
//            gl2.glActiveTexture(gl2.GL_TEXTURE1)
//            gl2.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureIds[1])
//        }
//    }

    private var program = 0

    private fun createProgram() {
        fun compileShader(type: Int): Int {
            val (path, operation) =
                if (type == gl2.GL_VERTEX_SHADER) {
                    Pair(
                        VERTEX_SHADER_PATH,
                        "Vertex shader compile"
                    )
                } else {
                    Pair(FRAGMENT_SHADER_PATH, "Fragment shader compile")
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
        setupHandlers()
    }

    private var positionHandle = 0
    private var textureCoordinateHandle = 0
    private var positionMatrixHandle = 0
    private var textureTransformHandle = 0
    private var textureHandle = 0

    private fun setupHandlers() {
        positionHandle = gl2.glGetAttribLocation(program, "aPosition")
        textureCoordinateHandle = gl2.glGetAttribLocation(program, "aTexCoordinate")
        positionMatrixHandle = gl2.glGetUniformLocation(program, "uMVPMatrix")
        textureTransformHandle = gl2.glGetUniformLocation(program, "uTextureTransform")
        textureHandle = gl2.glGetUniformLocation(program, "sTexture")
    }

    private inline fun withGlErrorChecking(operation: String? = null, glBlock: () -> Unit) {
        glBlock()
        val error = gl2.glGetError()
        if (BuildConfig.DEBUG && error != gl2.GL_NO_ERROR) {
            operation?.let { Log.e(TAG, "$it failed") }
            throw RuntimeException(GLUtils.getEGLErrorString(error))
        }
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

    interface StateListener {
        fun onRendererReady()
        fun onRendererFinished()
    }

    private companion object {

        val TAG = Renderer::class.java.simpleName

        const val VERTEX_SHADER_PATH = "shaders/tex.vert"
        const val FRAGMENT_SHADER_PATH = "shaders/tex.frag"

        fun floatArrayToBuffer(array: FloatArray): FloatBuffer = ByteBuffer.allocate(array.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(array).apply { position(0) }

        fun shortArrayToBuffer(array: ShortArray): ShortBuffer = ByteBuffer.allocate(array.size * 2)
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