#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <string>
#include <vector>
#include <algorithm>

// Header NCNN
#include "net.h"
#include "gpu.h"

#define TAG "KytheraNCNN"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// Global state
static ncnn::Net* g_net = nullptr;
static bool g_has_gpu = false;
static int g_scale = 2;

// Cached JNI references to avoid per-frame lookups
static jclass g_bitmap_class = nullptr;
static jmethodID g_create_bitmap_mid = nullptr;
static jobject g_argb8888_config = nullptr;

static void initJniCache(JNIEnv *env) {
    if (g_bitmap_class != nullptr) return;
    
    jclass localBitmap = env->FindClass("android/graphics/Bitmap");
    g_bitmap_class = (jclass)env->NewGlobalRef(localBitmap);
    g_create_bitmap_mid = env->GetStaticMethodID(g_bitmap_class, "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");

    jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
    jfieldID fid = env->GetStaticFieldID(configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    jobject localConfig = env->GetStaticObjectField(configClass, fid);
    g_argb8888_config = env->NewGlobalRef(localConfig);
}

// Tiled NCNN Inference for high-res stability & Vulkan VRAM optimization
static bool processMatTiled(ncnn::Net* net, const ncnn::Mat& in, ncnn::Mat& out, int tile_size = 256, int pad_size = 8) {
    int w = in.w;
    int h = in.h;
    int channels = in.c;
    int scale = g_scale;

    out.create(w * scale, h * scale, channels, (size_t)4u);
    if (out.empty()) return false;

    int tiles_x = (w + tile_size - 1) / tile_size;
    int tiles_y = (h + tile_size - 1) / tile_size;

    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            int x0 = tx * tile_size;
            int y0 = ty * tile_size;
            int x1 = std::min(x0 + tile_size, w);
            int y1 = std::min(y0 + tile_size, h);

            int px0 = std::max(x0 - pad_size, 0);
            int py0 = std::max(y0 - pad_size, 0);
            int px1 = std::min(x1 + pad_size, w);
            int py1 = std::min(y1 + pad_size, h);

            // Cut padded tile from input
            ncnn::Mat tile_in;
            ncnn::copy_cut_tile(in, tile_in, px0, py0, px1 - px0, py1 - py0);

            // Execute NCNN Extractor for tile
            ncnn::Extractor ex = net->create_extractor();
            ex.input("data", tile_in);
            ncnn::Mat tile_out;
            ex.extract("output", tile_out);

            if (tile_out.empty()) return false;

            // Calculate valid unpadded region inside tile_out
            int select_x = (x0 - px0) * scale;
            int select_y = (y0 - py0) * scale;
            int select_w = (x1 - x0) * scale;
            int select_h = (y1 - y0) * scale;

            int out_x = x0 * scale;
            int out_y = y0 * scale;

            // Copy valid tile output region to final output Mat
            for (int c = 0; c < channels; c++) {
                const float* src_ptr = tile_out.channel(c).row(select_y) + select_x;
                float* dst_ptr = out.channel(c).row(out_y) + out_x;
                for (int row = 0; row < select_h; row++) {
                    memcpy(dst_ptr, src_ptr, select_w * sizeof(float));
                    src_ptr += tile_out.w;
                    dst_ptr += out.w;
                }
            }
        }
    }
    return true;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_d4nzxml_kythera_service_NcnnVideoBridge_initEngine(JNIEnv *env, jclass clazz, jobject assetManager) {
    initJniCache(env);

    if (g_net != nullptr) {
        delete g_net;
        g_net = nullptr;
    }

    ncnn::create_gpu_instance();
    g_has_gpu = ncnn::get_gpu_count() > 0;

    g_net = new ncnn::Net();

    // Optimasi Maksimal GPU Vulkan & FP16 Execution
    g_net->opt.use_vulkan_compute = g_has_gpu;
    g_net->opt.use_fp16_packed = true;
    g_net->opt.use_fp16_storage = true;
    g_net->opt.use_fp16_arithmetic = true;
    g_net->opt.use_packing_layout = true;
    g_net->opt.use_shader_pack8 = true;
    g_net->opt.num_threads = 4;

    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    
    int ret_param = g_net->load_param(mgr, "realsr/models/realesr-animevideov3-x2.param");
    int ret_bin   = g_net->load_model(mgr, "realsr/models/realesr-animevideov3-x2.bin");

    if (ret_param != 0 || ret_bin != 0) {
        LOGE("Gagal load model NCNN dari assets! Cek path file.");
        return JNI_FALSE;
    }

    g_scale = 2;
    LOGI("NCNN Engine (FP16 Vulkan: %s) Loaded OK!", g_has_gpu ? "AKTIF" : "OFF");
    return JNI_TRUE;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_d4nzxml_kythera_service_NcnnVideoBridge_destroyEngine(JNIEnv *env, jclass clazz) {
    if (g_net != nullptr) {
        delete g_net;
        g_net = nullptr;
    }
    ncnn::destroy_gpu_instance();
    LOGI("Mesin NCNN dimatikan.");
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_d4nzxml_kythera_service_NcnnVideoBridge_processFrame(JNIEnv *env, jclass clazz, jobject bitmap, jboolean useGpu) {
    if (g_net == nullptr) {
        LOGE("Mesin NCNN belum nyala!");
        return nullptr;
    }

    initJniCache(env);

    AndroidBitmapInfo info;
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0 || AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) {
        LOGE("Gagal mengunci pixels dari Bitmap Kotlin.");
        return nullptr;
    }

    ncnn::Mat in = ncnn::Mat::from_pixels((const unsigned char*)pixels, ncnn::Mat::PIXEL_RGBA2RGB, info.width, info.height);
    AndroidBitmap_unlockPixels(env, bitmap);

    g_net->opt.use_vulkan_compute = g_has_gpu && useGpu;

    ncnn::Mat out;
    bool ok = false;
    
    // Tiling logic jika resolusi gambar > 320p untuk stabilitas VRAM GPU
    if (in.w > 320 || in.h > 320) {
        ok = processMatTiled(g_net, in, out, 256, 8);
    } else {
        ncnn::Extractor ex = g_net->create_extractor();
        ex.input("data", in);
        ex.extract("output", out);
        ok = !out.empty();
    }

    if (!ok || out.empty()) {
        LOGE("AI Upscale Extraction Failed!");
        return nullptr;
    }

    jobject resultBitmap = env->CallStaticObjectMethod(g_bitmap_class, g_create_bitmap_mid, out.w, out.h, g_argb8888_config);

    void* resultPixels = nullptr;
    if (AndroidBitmap_lockPixels(env, resultBitmap, &resultPixels) >= 0) {
        out.to_pixels((unsigned char*)resultPixels, ncnn::Mat::PIXEL_RGB2RGBA);
        AndroidBitmap_unlockPixels(env, resultBitmap);
    }

    return resultBitmap;
}

// Zero-Disk Direct Pipe Native Processing (Direct Byte Stream Buffer Write)
extern "C"
JNIEXPORT jboolean JNICALL
Java_com_d4nzxml_kythera_service_NcnnVideoBridge_processFrameToBuffer(
        JNIEnv *env, jclass clazz, jobject bitmap, jbyteArray outBuffer, jboolean useGpu) {
    if (g_net == nullptr) return JNI_FALSE;

    AndroidBitmapInfo info;
    void* pixels = nullptr;
    if (AndroidBitmap_getInfo(env, bitmap, &info) < 0 || AndroidBitmap_lockPixels(env, bitmap, &pixels) < 0) {
        return JNI_FALSE;
    }

    ncnn::Mat in = ncnn::Mat::from_pixels((const unsigned char*)pixels, ncnn::Mat::PIXEL_RGBA2RGB, info.width, info.height);
    AndroidBitmap_unlockPixels(env, bitmap);

    g_net->opt.use_vulkan_compute = g_has_gpu && useGpu;

    ncnn::Mat out;
    bool ok = false;
    if (in.w > 320 || in.h > 320) {
        ok = processMatTiled(g_net, in, out, 256, 8);
    } else {
        ncnn::Extractor ex = g_net->create_extractor();
        ex.input("data", in);
        ex.extract("output", out);
        ok = !out.empty();
    }

    if (!ok || out.empty()) return JNI_FALSE;

    jbyte* dstBuf = env->GetByteArrayElements(outBuffer, nullptr);
    if (dstBuf != nullptr) {
        // Output raw RGB24 directly to target buffer
        out.to_pixels((unsigned char*)dstBuf, ncnn::Mat::PIXEL_RGB2RGB);
        env->ReleaseByteArrayElements(outBuffer, dstBuf, 0);
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

