package com.gmail.tiomamaster.watermarkablecamera

import android.content.Intent
import android.os.Bundle
import android.widget.ArrayAdapter
import androidx.appcompat.app.AppCompatActivity
import com.gmail.tiomamaster.watermarkablecamera.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

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

        binding.btnGl.setOnClickListener {
            startActivity(
                Intent(
                    this,
                    GlCameraActivity::class.java
                )
            )
        }
        binding.btnVk.setOnClickListener {
            startActivity(
                Intent(
                    this,
                    VkCameraActivity::class.java
                )
            )
        }
    }
}