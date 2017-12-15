// Standard includes
#include <jni.h>
#include <string>
#include <algorithm>
#include <array>

// Caffe2 includes
#define PROTOBUF_USE_DLLS 1
#define CAFFE2_USE_LITE_PROTO 1

#include <caffe2/core/predictor.h>
#include <caffe2/core/operator.h>
#include <caffe2/core/timer.h>
#include <caffe2/core/init.h>
#include "caffe2/mobile/contrib/opengl/core/rewrite_net.h"
#include "caffe2/mobile/contrib/opengl/core/GLPredictor.h"

// Android includes
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

// libYUV
#include <libyuv.h>

// Macros
#define ANDROID_LOG(level, ...) __android_log_print(ANDROID_LOG_##level, "StyleTransfer", __VA_ARGS__);
//#define USE_OPEN_GL

namespace {

// Variables
const size_t kNumberOfChannels = 4; // RGBA/BGRA
const std::vector<std::string> kStyleNames = {"night_b", "flowers"};
std::vector<std::unique_ptr<caffe2::Predictor>> predictors;

caffe2::NetDef loadNet(AAssetManager *asset_manager, const std::string &filename) {
    AAsset *asset = AAssetManager_open(asset_manager, filename.c_str(), AASSET_MODE_BUFFER);
    const void *data = AAsset_getBuffer(asset);
    const off_t length = AAsset_getLength(asset);

    caffe2::NetDef net;
    if (net.ParseFromArray(data, length)) {
        ANDROID_LOG(INFO, "Successfully loaded NetDef %s", filename.c_str());
    } else {
        ANDROID_LOG(ERROR, "Couldn't parse net from data.");
    }

    AAsset_close(asset);

    return net;
}

caffe2::NetDef convertToOpenGL(caffe2::NetDef& init_net, caffe2::NetDef& predict_net) {
    try {
        ANDROID_LOG(INFO, "Converting prediction network to OpenGL implementation...");
        caffe2::NetDef gl_predict_net;
        CAFFE_ENFORCE(
                caffe2::tryConvertToOpenGL(init_net, predict_net, &gl_predict_net));
        return gl_predict_net;
    } catch (std::exception &exception) {
        ANDROID_LOG(ERROR, "OpenGL conversion failed with [%s], will be using CPU instead",
                    exception.what());
        return predict_net;
    }
}

} // namespace

extern "C"
void
Java_facebook_styletransfer_StyleTransfer_initCaffe2(
        JNIEnv *env,
        jobject /* this */,
        jobject jAssetManager) try {
    AAssetManager *asset_manager = AAssetManager_fromJava(env, jAssetManager);
    ANDROID_LOG(INFO, "Loading protobuf NetDefs...");
    for (const auto &name : kStyleNames) {
        auto init_net = loadNet(asset_manager, name + "/init_net.pb");
        auto predict_net = loadNet(asset_manager, name + "/predict_net.pb");

#ifdef USE_OPEN_GL
        predict_net = convertToOpenGL(init_net, predict_net);
#endif

        // Replace NNPack engine with default engine (faster for now).
        for (int op_index = 0; op_index < predict_net.op().size(); ++op_index) {
            predict_net.mutable_op(op_index)->set_engine("default");
        }

        predictors.emplace_back(new caffe2::Predictor(init_net, predict_net));
    }

    ANDROID_LOG(INFO, "Initialization successful!");
} catch (std::exception &exception) {
    ANDROID_LOG(ERROR, "StyleTransfer initialization failed: %s", exception.what());
    std::exit(EXIT_FAILURE);
}

extern "C"
JNIEXPORT jintArray JNICALL
Java_facebook_styletransfer_StyleTransfer_transformImageWithCaffe2(
        JNIEnv *env,
        jobject /* this */,
        jint styleIndex,
        jint height,
        jint width,
        jbyteArray YArray,
        jbyteArray UArray,
        jbyteArray VArray,
        jint UVRowStride,
        jint UVPixelStride) try {
    // For keeping track of average FPS.
    static size_t iterations = 0;
    static size_t total_fps = 0;

    CAFFE_ENFORCE(styleIndex >= 0 && styleIndex < predictors.size());

    caffe2::Timer timer;
    timer.Start();

    const jbyte *Y = env->GetByteArrayElements(YArray, nullptr);
    const jbyte *U = env->GetByteArrayElements(UArray, nullptr);
    const jbyte *V = env->GetByteArrayElements(VArray, nullptr);

    // Convert from YUV_420_8888 to ABGR.
    caffe2::TensorCPU image_tensor;
    image_tensor.Resize(1, height, width, kNumberOfChannels);
    int return_code = libyuv::Android420ToABGR(
            reinterpret_cast<const uint8_t *>(Y),
            width,
            reinterpret_cast<const uint8_t *>(U),
            UVRowStride,
            reinterpret_cast<const uint8_t *>(V),
            UVRowStride,
            UVPixelStride,
            image_tensor.mutable_data<uint8_t>(),
            width * 4,
            width,
            height);
    CAFFE_ENFORCE_EQ(return_code, 0);

    // Call the Caffe2 predictor.
    std::vector<caffe2::TensorCPU *> output;
    predictors[styleIndex]->run({&image_tensor}, &output);
    CAFFE_ENFORCE(!output.empty());

    const caffe2::TensorCPU *output_tensor = output.front();
    CAFFE_ENFORCE(output_tensor != nullptr);

    // "pack" ABGR pixels into ints.
    const int* pixel_buffer = reinterpret_cast<const int*>(output_tensor->data<uint8_t>());
    CAFFE_ENFORCE(pixel_buffer != nullptr);

    // Convert to native Java array.
    jintArray output_array = env->NewIntArray(output_tensor->size());
    CAFFE_ENFORCE(output_array != nullptr);
    env->SetIntArrayRegion(output_array, 0, output_tensor->size(), pixel_buffer);

    total_fps += timer.MilliSeconds();
    ANDROID_LOG(ERROR, "StyleTransfer Average FPS: %.3f", 1000.0/(total_fps / ++iterations));

    return output_array;

} catch (std::exception &exception) {
    ANDROID_LOG(ERROR, "Error performing StyleTransfer: %s", exception.what());
    return nullptr;
}