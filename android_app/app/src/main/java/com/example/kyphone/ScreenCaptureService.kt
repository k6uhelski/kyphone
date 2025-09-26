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
import kotlinx.coroutines.sync.Mutex
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.Socket

class ScreenCaptureService : Service() {

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private val tag = "CaptureService"
    private var clientSocket: Socket? = null
    private var outputStream: OutputStream? = null
    private val sendMutex = Mutex()
    private var lastSuccessfulBitmap: Bitmap? = null

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
        initializeCommunications()
        return START_NOT_STICKY
    }

    private fun initializeCommunications() {
        scope.launch {
            try {
                Log.d(tag, "Attempting to connect to proxy...")
                clientSocket = Socket("127.0.0.1", 65432)
                outputStream = clientSocket?.getOutputStream()
                Log.d(tag, "✅ Connected to proxy.")

                sendMutex.lock()
                listenForProxyMessages()
                startScreenCapture()

            } catch (e: Exception) {
                Log.e(tag, "Connection failed: ${e.message}")
                stopSelf()
            }
        }
    }

    private fun listenForProxyMessages() = scope.launch {
        Log.d(tag, "Proxy message listener started.")
        try {
            val reader = BufferedReader(InputStreamReader(clientSocket!!.getInputStream()))
            while (isActive) {
                val message = reader.readLine()
                if (message == null) {
                    Log.w(tag, "Proxy disconnected.")
                    break
                }

                if (message == "ACK") {
                    Log.d(tag, "Proxy -> App: ACK received.")
                    if (sendMutex.isLocked) sendMutex.unlock()
                } else {
                    Log.d(tag, "Proxy -> App: Received coordinates: $message")
                }
            }
        } catch (e: Exception) {
            Log.e(tag, "Listener error: ${e.message}")
        }
    }

    private fun startScreenCapture() {
        val windowManager = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val metrics = DisplayMetrics()
        scope.launch {
            val display = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val displayManager = getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
                displayManager.getDisplay(Display.DEFAULT_DISPLAY)
            } else {
                @Suppress("DEPRECATION")
                windowManager.defaultDisplay
            }
            display?.getRealMetrics(metrics)
            val screenWidth = metrics.widthPixels
            val screenHeight = metrics.heightPixels
            val screenDensity = metrics.densityDpi
            imageReader = ImageReader.newInstance(screenWidth, screenHeight, PixelFormat.RGBA_8888, 2)
            virtualDisplay = mediaProjection?.createVirtualDisplay(
                "KyPhoneCapture",
                screenWidth, screenHeight, screenDensity,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                imageReader?.surface, null, null
            )
            Log.d(tag, "Screen capture started. Waiting for ACK to send.")

            // ++ MODIFIED: The loop now checks the return value of captureAndSend
            while (isActive) {
                sendMutex.lock()
                val success = captureAndSend(imageReader!!)
                if (!success) {
                    // If we failed to send (e.g., no frame), we must unlock to try again.
                    if (sendMutex.isLocked) sendMutex.unlock()
                    // Add a small delay to prevent a fast spin-lock
                    delay(100)
                }
            }
        }
    }

    // ++ MODIFIED: Function now returns a Boolean indicating success
    private fun captureAndSend(reader: ImageReader): Boolean {
        val image = reader.acquireLatestImage()
        var success = false
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
                success = true // Mark as successful
            } else {
                Log.d(tag, "No frame available to send yet.")
                success = false // Mark as failed
            }
        } catch (e: Exception) {
            Log.e(tag, "Error during capture and send", e)
            success = false
        } finally {
            image?.close()
        }
        return success
    }

    private fun sendImageOverNetwork(imageData: ByteArray) {
        scope.launch {
            try {
                outputStream?.apply {
                    write(imageData)
                    flush()
                    Log.d(tag, "App -> Proxy: Sent ${imageData.size} bytes.")
                }
            } catch (e: Exception) {
                Log.e(tag, "Image send error: ${e.message}")
            }
        }
    }

    private fun stopScreenCapture() {
        scope.cancel()
        virtualDisplay?.release()
        imageReader?.close()
        mediaProjection?.unregisterCallback(mediaProjectionCallback)
        mediaProjection?.stop()
        clientSocket?.close()
        lastSuccessfulBitmap = null
        stopForeground(true)
        stopSelf()
        Log.d(tag, "Screen capture resources released.")
    }

    private fun processBitmap(bitmap: Bitmap): Bitmap {
        val originalWidth = bitmap.width
        val originalHeight = bitmap.height
        val cropSize = if (originalWidth < originalHeight) originalWidth else originalHeight
        val cropX = (originalWidth - cropSize) / 2
        val cropY = (originalHeight - cropSize) / 2
        val croppedBitmap = Bitmap.createBitmap(bitmap, cropX, cropY, cropSize, cropSize)
        return Bitmap.createScaledBitmap(croppedBitmap, 600, 600, true)
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

    private fun startForegroundWithNotification() {
        val channelId = "capture_channel"
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(channelId, "Screen Capture", NotificationManager.IMPORTANCE_DEFAULT)
            getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
        }
        val notification = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            Notification.Builder(this, channelId)
                .setContentTitle("KyPhone")
                .setContentText("Bidirectional link active.")
                .setSmallIcon(R.mipmap.ic_launcher)
                .build()
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
                .setContentTitle("KyPhone")
                .setContentText("Bidirectional link active.")
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