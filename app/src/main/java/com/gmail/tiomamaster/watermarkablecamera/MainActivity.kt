package com.gmail.tiomamaster.watermarkablecamera

import android.Manifest
import android.annotation.SuppressLint
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.gmail.tiomamaster.watermarkablecamera.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity(), AdapterView.OnItemSelectedListener {

    private lateinit var binding: ActivityMainBinding

    private val hasPermissions: Boolean
        get() = PERMISSIONS.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }

    private lateinit var onPermissionsGranted: () -> Unit

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.spinQuality.apply {
            adapter = ArrayAdapter(
                this@MainActivity,
                R.layout.item_spinner_quality,
                Resolution.entries.map(Resolution::title)
            )
            onItemSelectedListener = this@MainActivity
            setSelection(1) // set FullHD by default
        }

        binding.btnGl.setOnClickListener(::onOpenCameraClick)
        binding.btnVk.setOnClickListener(::onOpenCameraClick)
    }

    @SuppressLint("SetTextI18n")
    override fun onItemSelected(
        parent: AdapterView<*>?,
        view: View?,
        position: Int,
        id: Long
    ) {
        val size = Resolution.entries[position].size
        binding.txtSelectedResolution.text = "${size.width}x${size.height}"
    }

    override fun onNothingSelected(parent: AdapterView<*>?) = Unit

    fun onOpenCameraClick(v: View) {
        val cls = when (v.id) {
            binding.btnGl.id -> GlCameraActivity::class.java
            binding.btnVk.id -> VkCameraActivity::class.java
            else -> throw IllegalArgumentException("Wrong id for view passed in onOpenCameraClick")
        }
        onPermissionsGranted = {
            startActivity(
                Intent(this, cls).apply {
                    putExtra(EXTRA_RESOLUTION, binding.spinQuality.selectedItemPosition)
                }
            )
        }
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

    companion object {
        const val EXTRA_RESOLUTION = "EXTRA_RESOLUTION"
        private val PERMISSIONS =
            arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
        private const val REQUEST_CODE_PERMISSIONS = 0xCA
    }
}