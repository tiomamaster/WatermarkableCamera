package com.gmail.tiomamaster.watermarkablecamera

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.gmail.tiomamaster.watermarkablecamera.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private val hasPermissions: Boolean
        get() = PERMISSIONS.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    private lateinit var onPermissionsGranted: () -> Unit

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.spinQuality.adapter =
            ArrayAdapter(
                this,
                R.layout.item_spinner_quality,
                listOf("1920x1080 FullHD", "1280x720 HD", "640x480 SD")
            )

        binding.btnGl.setOnClickListener(::onOpenCameraClick)
        binding.btnVk.setOnClickListener(::onOpenCameraClick)
    }

    fun onOpenCameraClick(v: View) {
        val cls = when (v.id) {
            binding.btnGl.id -> GlCameraActivity::class.java
            binding.btnVk.id -> VkCameraActivity::class.java
            else -> throw IllegalArgumentException("Wrong id for view passed in onOpenCameraClick")
        }
        onPermissionsGranted = { startActivity(Intent(this, cls)) }
        if (!checkAndRequestPermissions()) return
        onPermissionsGranted()
    }

    private fun checkAndRequestPermissions(): Boolean =
        if (!hasPermissions) {
            requestPermissions(
                PERMISSIONS,
                REQUEST_CODE_PERMISSIONS
            )
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
        if (requestCode == REQUEST_CODE_PERMISSIONS && grantResults.size > 1 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED &&
            grantResults[1] == PackageManager.PERMISSION_GRANTED
        ) {
            onPermissionsGranted()
        } else {
            Toast.makeText(
                this,
                "Permissions are not granted",
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private companion object {
        val PERMISSIONS = arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        const val REQUEST_CODE_PERMISSIONS = 0xCA
    }
}