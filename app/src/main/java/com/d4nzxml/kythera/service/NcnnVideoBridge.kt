package com.d4nzxml.kythera.service

import android.content.Context
import android.content.res.AssetManager
import android.graphics.Bitmap
import android.util.Log

class NcnnVideoBridge {
    // 1. Enum Accelerator & Scale
    enum class Accelerator(val label: String, val desc: String) {
        CPU("CPU", "Aman tapi lambat"),
        GPU("Vulkan GPU (FP16)", "Kencang (Akselerasi Vulkan FP16)") 
    }

    enum class VideoScale(val label: String) {
        X2("Scale 2x"),
        X4("Scale 4x")
    }

    companion object {
        private const val TAG = "NcnnBridgeKotlin"

        init {
            try {
                System.loadLibrary("ncnn_bridge") 
                Log.d(TAG, "Library NCNN sukses di-load!")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Waduh, gagal nge-load library C++: ${e.message}")
            }
        }

        // 2. Fungsi pembantu (wrapper)
        fun setup(context: Context, scale: VideoScale): Boolean {
            return initEngine(context.assets)
        }

        fun switchScale(context: Context, scale: VideoScale): Boolean {
            return true
        }

        fun enhance(frame: Bitmap, accelerator: Accelerator): Bitmap? {
            val useGpu = accelerator == Accelerator.GPU
            return processFrame(frame, useGpu)
        }

        fun enhanceToBuffer(frame: Bitmap, outBuffer: ByteArray, accelerator: Accelerator): Boolean {
            val useGpu = accelerator == Accelerator.GPU
            return processFrameToBuffer(frame, outBuffer, useGpu)
        }

        // 3. Deklarasi fungsi JNI asli yang nyambung ke C++
        @JvmStatic external fun initEngine(assetManager: AssetManager): Boolean
        
        @JvmStatic external fun destroyEngine()
        
        @JvmStatic external fun processFrame(bitmap: Bitmap, useGpu: Boolean): Bitmap?

        @JvmStatic external fun processFrameToBuffer(bitmap: Bitmap, outBuffer: ByteArray, useGpu: Boolean): Boolean
    }
}

