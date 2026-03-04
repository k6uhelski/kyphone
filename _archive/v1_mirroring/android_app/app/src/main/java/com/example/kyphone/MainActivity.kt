package com.example.kyphone

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjectionManager
import android.os.Bundle
import android.util.Log
import android.widget.Button
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {

    private val tag = "MainActivity"

    // --- THIS IS THE NEW, CORRECT WAY TO GET PERMISSION ---
    private val screenCaptureLauncher =
        registerForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                val resultCode = result.resultCode
                val data = result.data

                if (resultCode == Activity.RESULT_OK && data != null) {
                    Log.d(tag, "Screen capture permission GRANTED")

                    // --- THIS IS THE FIX ---
                    // Create the intent and add the permissions as extras
                    val serviceIntent = Intent(this, ScreenCaptureService::class.java).apply {
                        putExtra("resultCode", resultCode)
                        putExtra("data", data)
                    }

                    startForegroundService(serviceIntent)
                    finish() // Optional: close the activity after starting
                } else {
                    Log.e(tag, "Screen capture permission DENIED")
                }
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // --- THIS IS THE CORRECTED LINE ---
        // Find your "Start Mirroring" button
        val startButton: Button = findViewById(R.id.sendButton) // Corrected ID

        startButton.setOnClickListener {
            startScreenCapturePermissionRequest()
        }
    }

    private fun startScreenCapturePermissionRequest() {
        val mediaProjectionManager =
            getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager

        // Create the permission request intent
        val captureIntent = mediaProjectionManager.createScreenCaptureIntent()

        // Launch the activity to get permission
        screenCaptureLauncher.launch(captureIntent)
    }
}