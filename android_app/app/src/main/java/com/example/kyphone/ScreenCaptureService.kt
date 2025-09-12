package com.example.kyphone

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.PixelFormat
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.ImageReader
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.DisplayMetrics
import android.util.Log
import android.view.Display
import android.view.WindowManager
import kotlinx.coroutines.*
import java.io.OutputStream
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

class ScreenCaptureService : Service() {

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private val scope = CoroutineScope(Dispatchers.IO)
    private val tag = "CaptureService"
    private val isProcessingFrame = AtomicBoolean(false)

    private val mediaProjectionCallback = object : MediaProjection.Callback() {
        override fun onStop() {
            super.onStop()
            Log.d(tag, "MediaProjection stopped by user.")
            stopScreenCapture()
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(tag, "Service starting.")
        startForegroundWithNotification()

        val resultCode = intent!!.getIntExtra("resultCode", -1)
        val data = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra("data", Intent::class.java)!!
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra("data")!!
        }

        val mediaProjectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        mediaProjection = mediaProjectionManager.getMediaProjection(resultCode, data)
        mediaProjection?.registerCallback(mediaProjectionCallback, Handler(Looper.getMainLooper()))

        startScreenCapture()

        return START_NOT_STICKY
    }

    private fun startScreenCapture() {
        val windowManager = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val metrics = DisplayMetrics()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val displayManager = getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
            val display = displayManager.getDisplay(Display.DEFAULT_DISPLAY)
            display?.getRealMetrics(metrics)
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.getMetrics(metrics)
        }

        val screenWidth = metrics.widthPixels
        val screenHeight = metrics.heightPixels
        val screenDensity = metrics.densityDpi

        // --- Use an event listener that fires automatically when a new image is ready. ---
        imageReader = ImageReader.newInstance(screenWidth, screenHeight, PixelFormat.RGBA_8888, 2)
        imageReader?.setOnImageAvailableListener({ reader ->
            // If we are already processing a frame, ignore this new one to prevent backlog.
            if (isProcessingFrame.compareAndSet(false, true)) {
                scope.launch {
                    captureAndSend(reader)
                }
            }
        }, Handler(Looper.getMainLooper()))

        virtualDisplay = mediaProjection?.createVirtualDisplay(
            "KyPhoneCapture",
            screenWidth, screenHeight, screenDensity,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            imageReader?.surface, null, null
        )
    }

    private fun stopScreenCapture() {
        if (scope.isActive) {
            scope.cancel()
            virtualDisplay?.release()
            imageReader?.close()
            mediaProjection?.unregisterCallback(mediaProjectionCallback)
            mediaProjection?.stop()
            stopForeground(true)
            stopSelf()
            Log.d(tag, "Screen capture resources released.")
        }
    }

    private fun captureAndSend(reader: ImageReader) {
        val image = reader.acquireNextImage()
        try {
            if (image != null) {
                val planes = image.planes
                val buffer = planes[0].buffer
                val pixelStride = planes[0].pixelStride
                val rowStride = planes[0].rowStride
                val rowPadding = rowStride - pixelStride * image.width

                val bitmap = Bitmap.createBitmap(image.width + rowPadding / pixelStride, image.height, Bitmap.Config.ARGB_8888)
                bitmap.copyPixelsFromBuffer(buffer)

                val processedBitmap = processBitmap(bitmap)
                val imageData = convertBitmapTo1Bit(processedBitmap)
                sendImageOverNetwork(imageData)
            }
        } catch (e: Exception) {
            Log.e(tag, "Error during capture and send", e)
        } finally {
            image?.close()
            // Release the lock so the next frame can be processed.
            isProcessingFrame.set(false)
        }
    }

    private fun processBitmap(bitmap: Bitmap): Bitmap {
        return Bitmap.createScaledBitmap(bitmap, 600, 600, true)
    }

    private fun convertBitmapTo1Bit(bitmap: Bitmap): ByteArray {
        val width = bitmap.width
        val height = bitmap.height
        val buffer = ByteArray((width * height) / 8)
        for (y in 0 until height) {
            for (x in 0 until width) {
                val pixel = bitmap.getPixel(x, y)
                val brightness = (Color.red(pixel) + Color.green(pixel) + Color.blue(pixel)) / 3
                if (brightness < 128) {
                    val byteIndex = (y * width + x) / 8
                    val bitIndex = 7 - (x % 8)
                    buffer[byteIndex] = (buffer[byteIndex].toInt() or (1 shl bitIndex)).toByte()
                }
            }
        }
        for (i in buffer.indices) {
            buffer[i] = buffer[i].toInt().inv().toByte()
        }
        return buffer
    }

    private fun sendImageOverNetwork(imageData: ByteArray) {
        try {
            val socket = Socket("127.0.0.1", 65432)
            val outputStream: OutputStream = socket.getOutputStream()
            val fullCommand = "START\nIMG_DATA\n".toByteArray() + imageData
            outputStream.write(fullCommand)
            outputStream.flush()
            socket.close()
            Log.d(tag, "Sent ${imageData.size} bytes successfully.")
        } catch (e: Exception) {
            Log.e(tag, "Network send error", e)
        }
    }

    private fun startForegroundWithNotification() {
        val channelId = "capture_channel"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(channelId, "Screen Capture", NotificationManager.IMPORTANCE_DEFAULT)
            val manager = getSystemService(NotificationManager::class.java)
            manager.createNotificationChannel(channel)
        }

        val notification = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, channelId)
                .setContentTitle("KyPhone Display")
                .setContentText("Mirroring screen to Inkplate.")
                .setSmallIcon(R.mipmap.ic_launcher)
                .build()
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
                .setContentTitle("KyPhone Display")
                .setContentText("Mirroring screen to Inkplate.")
                .setSmallIcon(R.mipmap.ic_launcher)
                .build()
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(1, notification, FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } else {
            startForeground(1, notification)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopScreenCapture()
    }
}

