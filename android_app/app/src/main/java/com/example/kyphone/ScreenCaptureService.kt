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
import java.io.InputStream
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
    private var lastSuccessfulBitmap: Bitmap? = null
    private val SERVICE_VERSION = "v2.0_final\n" // Version identifier

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
        imageReader = ImageReader.newInstance(screenWidth, screenHeight, PixelFormat.RGBA_8888, 2)
        scope.launch {
            while (isActive) {
                if (isProcessingFrame.compareAndSet(false, true)) {
                    captureAndSend(imageReader!!)
                }
                delay(3000)
            }
        }
        virtualDisplay = mediaProjection?.createVirtualDisplay(
            "KyPhoneCapture",
            screenWidth, screenHeight, screenDensity,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            imageReader?.surface, null, null
        )
    }

    private fun captureAndSend(reader: ImageReader) {
        val image = reader.acquireLatestImage()
        try {
            var bitmapToSend: Bitmap? = null
            if (image != null) {
                Log.d(tag, "New frame captured.")
                val planes = image.planes
                val buffer = planes[0].buffer
                val pixelStride = planes[0].pixelStride
                val rowStride = planes[0].rowStride
                val rowPadding = rowStride - pixelStride * image.width
                val rawBitmap = Bitmap.createBitmap(image.width + rowPadding / pixelStride, image.height, Bitmap.Config.ARGB_8888)
                rawBitmap.copyPixelsFromBuffer(buffer)
                lastSuccessfulBitmap = processBitmap(rawBitmap)
                bitmapToSend = lastSuccessfulBitmap
            } else if (lastSuccessfulBitmap != null) {
                Log.d(tag, "No new frame, re-sending last successful frame.")
                bitmapToSend = lastSuccessfulBitmap
            }
            if (bitmapToSend != null) {
                val imageData = convertBitmapTo1Bit(bitmapToSend)
                sendImageOverNetwork(imageData)
            } else {
                Log.d(tag, "No frame available to send yet.")
            }
        } catch (e: Exception) {
            Log.e(tag, "Error during capture and send", e)
        } finally {
            image?.close()
            isProcessingFrame.set(false)
        }
    }

    private fun stopScreenCapture() {
        if (scope.isActive) {
            scope.cancel()
            virtualDisplay?.release()
            imageReader?.close()
            mediaProjection?.unregisterCallback(mediaProjectionCallback)
            mediaProjection?.stop()
            lastSuccessfulBitmap = null
            stopForeground(true)
            stopSelf()
            Log.d(tag, "Screen capture resources released.")
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
            Socket("127.0.0.1", 65432).use { socket ->
                socket.soTimeout = 5000

                val outputStream: OutputStream = socket.getOutputStream()
                val inputStream: InputStream = socket.getInputStream()

                outputStream.write(SERVICE_VERSION.toByteArray())
                outputStream.write(imageData)
                outputStream.flush()
                Log.d(tag, "Sent version '$SERVICE_VERSION' and ${imageData.size} bytes.")

                val response = inputStream.read()
                if (response != -1) {
                    Log.d(tag, "Received OK acknowledgment from server.")
                } else {
                    Log.w(tag, "Server closed connection without sending OK.")
                }
            }
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