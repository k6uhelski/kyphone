package com.example.kyphone

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.app.*
import android.content.Context
import android.content.Intent
// --- THIS IS THE FIX ---
import android.content.pm.ServiceInfo
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Path
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
import android.view.accessibility.AccessibilityEvent
import kotlinx.coroutines.*
import kotlinx.coroutines.sync.Mutex
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.net.Socket

class ScreenCaptureService : AccessibilityService() {

    private var mediaProjection: MediaProjection? = null
    private var virtualDisplay: VirtualDisplay? = null
    private var imageReader: ImageReader? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private val tag = "CaptureService"
    private var clientSocket: Socket? = null
    private var outputStream: OutputStream? = null

    private val sendMutex = Mutex()
    private var isFirstAckReceived = false

    private var lastSentBitmap: Bitmap? = null
    private var isCaptureInitialized = false

    private var currentGesturePath: Path? = null

    private val mediaProjectionCallback = object : MediaProjection.Callback() {
        override fun onStop() {
            super.onStop()
            Log.d(tag, "MediaProjection stopped by user.")
            stopScreenCapture()
        }
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        // We don't need to react to events, just perform gestures
    }

    override fun onInterrupt() {
        // Handle interruption
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        Log.d(tag, "Service starting.")
        startForegroundWithNotification()

        if (intent == null) {
            Log.w(tag, "onStartCommand received null intent (likely system restart).")
            return START_NOT_STICKY
        }

        val resultCode = intent.getIntExtra("resultCode", Activity.RESULT_CANCELED)
        val data: Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra("data", Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra("data")
        }

        if (resultCode == Activity.RESULT_OK && data != null) {

            if (isCaptureInitialized) {
                Log.d(tag, "Capture already initialized. Ignoring new permission intent.")
                return START_NOT_STICKY
            }

            Log.d(tag, "Got permission extras. Initializing media projection.")
            isCaptureInitialized = true

            val mediaProjectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
            mediaProjection = mediaProjectionManager.getMediaProjection(resultCode, data)
            mediaProjection?.registerCallback(mediaProjectionCallback, Handler(Looper.getMainLooper()))

            initializeCommunications()
        } else {
            Log.d(tag, "onStartCommand called without permission extras (system re-bind?). Ignoring.")
        }

        return START_NOT_STICKY
    }

    private fun initializeCommunications() {
        scope.launch {
            try {
                Log.d(tag, "Attempting to connect to proxy...")
                clientSocket = Socket("10.0.2.2", 65432)
                outputStream = clientSocket?.getOutputStream()
                Log.d(tag, "✅ Connected to proxy.")

                listenForProxyMessages()

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
                    if (sendMutex.isLocked) {
                        sendMutex.unlock()
                    }

                    if (!isFirstAckReceived) {
                        isFirstAckReceived = true
                        Log.d(tag, "First ACK received, starting capture loop.")
                        startScreenCapture()
                    }
                    continue
                }

                try {
                    val parts = message.split(':')
                    if (parts.size < 2) continue

                    val command = parts[0]
                    val coords = parts[1].split(',')
                    if (coords.size < 2) continue

                    val x = coords[0].toFloat()
                    val y = coords[1].toFloat()

                    when (command) {
                        "DOWN" -> {
                            Log.d(tag, "Gesture DOWN at ($x, $y)")
                            currentGesturePath = Path().apply {
                                moveTo(x, y)
                            }
                        }
                        "DRAG" -> {
                            Log.d(tag, "Gesture DRAG to ($x, $y)")
                            currentGesturePath?.lineTo(x, y)
                        }
                        "UP" -> {
                            Log.d(tag, "Gesture UP")
                            currentGesturePath?.let {
                                if (it.isEmpty) {
                                    it.moveTo(x, y)
                                }
                                val duration = if (it.isEmpty) 1L else 200L
                                performGesture(it, duration)
                            }
                            currentGesturePath = null
                        }
                        else -> {
                            Log.w(tag, "Received unknown proxy message: $message")
                        }
                    }

                } catch (e: Exception) {
                    Log.e(tag, "Failed to parse command: $message, Error: ${e.message}")
                }
            }
        } catch (e: Exception) {
            Log.e(tag, "Listener error: ${e.message}")
        }
    }

    private fun performGesture(path: Path, duration: Long) {
        Log.d(tag, "App -> System: Injecting gesture with duration $duration ms")

        val gestureBuilder = GestureDescription.Builder()
        gestureBuilder.addStroke(GestureDescription.StrokeDescription(path, 0, duration))

        dispatchGesture(gestureBuilder.build(), object : GestureResultCallback() {
            override fun onCompleted(gestureDescription: GestureDescription?) {
                super.onCompleted(gestureDescription)
                Log.d(tag, "Gesture completed.")
            }
            override fun onCancelled(gestureDescription: GestureDescription?) {
                super.onCancelled(gestureDescription)
                Log.d(tag, "Gesture cancelled.")
            }
        }, null)
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

            while (isActive) {
                sendMutex.lock()
                val success = captureAndSend(imageReader!!)
                if (!success) {
                    if (sendMutex.isLocked) sendMutex.unlock()
                    delay(100)
                }
            }
        }
    }

    private suspend fun captureAndSend(reader: ImageReader): Boolean {
        val image = reader.acquireLatestImage()
        var success = false
        var bitmapToSend: Bitmap? = null

        try {
            if (image != null) {
                Log.d(tag, "New frame captured.")
                val planes = image.planes
                val buffer = planes[0].buffer
                val pixelStride = planes[0].pixelStride
                val rowStride = planes[0].rowStride
                val rowPadding = rowStride - pixelStride * image.width
                val rawBitmap = Bitmap.createBitmap(image.width + rowPadding / pixelStride, image.height, Bitmap.Config.ARGB_8888)
                rawBitmap.copyPixelsFromBuffer(buffer)

                bitmapToSend = processBitmap(rawBitmap)
            }

            if (bitmapToSend != null) {
                val isFirstFrame = lastSentBitmap == null
                val areBitmapsDifferent = lastSentBitmap?.sameAs(bitmapToSend) == false

                if (isFirstFrame || areBitmapsDifferent) {
                    val imageData = convertBitmapTo1Bit(bitmapToSend)
                    sendImageOverNetwork(imageData)

                    lastSentBitmap = bitmapToSend.copy(bitmapToSend.config, false)
                    success = true
                    Log.d(tag, "Change detected. Sending new frame.")

                } else {
                    Log.d(tag, "No change detected, skipping send.")
                    if (sendMutex.isLocked) sendMutex.unlock()
                    success = true
                }

            } else {
                Log.d(tag, "No frame available to send yet.")
                if (sendMutex.isLocked) sendMutex.unlock()
                success = false
            }
        } catch (e: Exception) {
            Log.e(tag, "Error during capture and send", e)
            if (sendMutex.isLocked) sendMutex.unlock()
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
                if (sendMutex.isLocked) sendMutex.unlock()
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
        lastSentBitmap = null
        isCaptureInitialized = false
        stopForeground(true)
        stopSelf()
        Log.d(tag, "Screen capture resources released.")
    }

    private fun processBitmap(bitmap: Bitmap): Bitmap {
        val originalWidth = bitmap.width
        val originalHeight = bitmap.height

        val statusBarOffset = 100

        if (originalHeight <= statusBarOffset) {
            Log.w(tag, "Bitmap height is smaller than status bar offset, scaling full image.")
            return Bitmap.createScaledBitmap(bitmap, 600, 600, true)
        }

        val heightAfterCrop = originalHeight - statusBarOffset
        val cropSize = if (originalWidth < heightAfterCrop) originalWidth else heightAfterCrop
        val cropX = (originalWidth - cropSize) / 2
        val cropY = statusBarOffset + ((heightAfterCrop - cropSize) / 2)

        val croppedBitmap = Bitmap.createBitmap(bitmap, cropX, cropY, cropSize, cropSize)
        return Bitmap.createScaledBitmap(croppedBitmap, 600, 600, true)
    }

    private suspend fun convertBitmapTo1Bit(bitmap: Bitmap): ByteArray = withContext(Dispatchers.Default) {
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
        buffer
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

        // --- THIS IS THE FIX ---
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(1, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
        } else {
            startForeground(1, notification)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        stopScreenCapture()
    }
}