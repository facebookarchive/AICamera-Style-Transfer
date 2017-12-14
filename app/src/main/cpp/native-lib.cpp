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

#define ANDROID_LOG(level, ...) __android_log_print(ANDROID_LOG_##level, "StyleTransfer", __VA_ARGS__);


namespace {
// Constants
const size_t kNumberOfChannels = 4; // RGBA/BGRA
const std::array<std::string, 10> kStyleNames = {
        "animals",
        "composition",
        "crayon",
        "flowers",
        "lines",
        "mosaic",
        "night_b",
        "page",
        "watercolor",
        "whale_c",
};

// Global variables
std::vector<std::pair<caffe2::NetDef, caffe2::NetDef>> net_defs;
std::unique_ptr<caffe2::Predictor> predictor;
int current_style_index = -1;
//caffe2::GLPredictor* openGLPredictor;

std::vector<int> packRGBAPixels(const caffe2::TensorCPU& tensor, int height, int width) {
    std::vector<int> packed;
    packed.reserve(width * height);

    const uint8_t* data = tensor.data<uint8_t>();

    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const auto b = (row * width * 4) + (column * 4) + 0;
            const auto g = (row * width * 4) + (column * 4) + 1;
            const auto r = (row * width * 4) + (column * 4) + 2;

            // Set the alpha channel to 255.
            int pixel = 0xff000000;

            // These channel values will be between 0 and 255.
            pixel |= static_cast<int>(data[r]) << 16;
            pixel |= static_cast<int>(data[g]) << 8;
            pixel |= static_cast<int>(data[b]);

            packed.push_back(pixel);
        }
    }

    return packed;
}


caffe2::NetDef loadNet(AAssetManager* asset_manager, const std::string& filename) {
    AAsset* asset = AAssetManager_open(asset_manager, filename.c_str(), AASSET_MODE_BUFFER);
    const void* data = AAsset_getBuffer(asset);
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


caffe2::TensorCPU* transformImage(caffe2::Predictor& predictor, caffe2::TensorCPU& image_tensor) {
    std::vector<caffe2::TensorCPU*> output;
    predictor.run({&image_tensor}, &output);
    CAFFE_ENFORCE(!output.empty());
    return output.front();
}

jintArray toIntArray(JNIEnv *env, const std::vector<int>& output_buffer) {
    jintArray output_array = env->NewIntArray(output_buffer.size());
    if (output_array != nullptr) {
        env->SetIntArrayRegion(output_array, 0, output_buffer.size(), output_buffer.data());
    }

    return output_array;
}

// A function to load the NetDefs from protobufs.
void loadToNetDef(AAssetManager* mgr, caffe2::NetDef* net, const char *filename) {
    AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
    assert(asset != nullptr);
    const void *data = AAsset_getBuffer(asset);
    assert(data != nullptr);
    off_t len = AAsset_getLength(asset);
    assert(len != 0);
    if (!net->ParseFromArray(data, len)) {
        ANDROID_LOG(ERROR, "Couldn't parse net from data.\n");
    }
    AAsset_close(asset);
}
} // namespace

extern "C"
void
Java_facebook_styletransfer_StyleTransfer_initCaffe2(
        JNIEnv *env,
        jobject /* this */,
        jobject jAssetManager) {
    AAssetManager* asset_manager = AAssetManager_fromJava(env, jAssetManager);

    ANDROID_LOG(INFO, "Loading protobuf NetDefs...");
    for (const auto& name : kStyleNames) {
        auto init_net = loadNet(asset_manager, name + "/init_net.pb");
        auto predict_net = loadNet(asset_manager, name + "/predict_net.pb");
        net_defs.emplace_back(std::move(init_net), std::move(predict_net));
    }

//    ANDROID_LOG(INFO, "Running initialization network...");
//    CAFFE_ENFORCE(workspace.RunNetOnce(init_net));

//    try {
//        openGLPredictor = new caffe2::GLPredictor(init_net, predict_net);
//        ANDROID_LOG(ERROR, "StyleTransfer asdfsdfasdf: %s", openGLPredictor->def().DebugString().c_str());
//    } catch(...) {
//        ANDROID_LOG(INFO, "BAD$$$$$$$$$$$$$$$$$$$$$$");
//    }

//    _predictor = new caffe2::Predictor(init_net, predict_net);

//    ANDROID_LOG(INFO, "Converting to OpenGL network...");
//    caffe2::NetDef gl_net_def;
//    CAFFE_ENFORCE(caffe2::tryConvertToOpenGL(init_net, predict_net, &gl_net_def));
//
//    ANDROID_LOG(INFO, "Creating Network...");
//    net = workspace.CreateNet(gl_net_def);
//    CAFFE_ENFORCE(net != nullptr);
//
//    ANDROID_LOG(INFO, "Initialization complete!");
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
        jint UVPixelStride) {

    CAFFE_ENFORCE(styleIndex >= 0 && styleIndex < net_defs.size());

    if (!predictor) {
        ANDROID_LOG(WARN, "Predictor was null");
    }

    if (styleIndex != current_style_index) {
        ANDROID_LOG(INFO, "Switching style to %s", kStyleNames[styleIndex].c_str());
        predictor.reset(new caffe2::Predictor(net_defs[styleIndex].first, net_defs[styleIndex].second));
        current_style_index = styleIndex;
    }

    const jbyte *Y = env->GetByteArrayElements(YArray, nullptr);
    const jbyte *U = env->GetByteArrayElements(UArray, nullptr);
    const jbyte *V = env->GetByteArrayElements(VArray, nullptr);

    ANDROID_LOG(INFO, "Converting from YUV To ABGR...");
    caffe2::TensorCPU image_tensor;
    image_tensor.Resize(1, height, width, kNumberOfChannels);
    int return_code = libyuv::Android420ToABGR(
            reinterpret_cast<const uint8_t*>(Y),
            width,
            reinterpret_cast<const uint8_t*>(U),
            UVRowStride,
            reinterpret_cast<const uint8_t*>(V),
            UVRowStride,
            UVPixelStride,
            image_tensor.mutable_data<uint8_t>(),
            width * 4,
            width,
            height);
    CAFFE_ENFORCE_EQ(return_code, 0);

    ANDROID_LOG(INFO, "Transforming image...");
    const caffe2::TensorCPU* output_tensor = transformImage(*predictor, image_tensor);

    ANDROID_LOG(INFO, "Packing RGB pixels...");
    const std::vector<int> output_buffer = packRGBAPixels(*output_tensor, height, width);

    ANDROID_LOG(INFO, "Copying to output...");
    return toIntArray(env, output_buffer);
}