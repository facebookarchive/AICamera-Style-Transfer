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

// Android includes
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

// libYUV
#include <libyuv.h>

#define ANDROID_LOG(level, ...) __android_log_print(ANDROID_LOG_##level, "StyleTransfer", __VA_ARGS__);

struct NetworkDefinition {
    std::string init_net;
    std::string predict_net;
};

const std::array<NetworkDefinition, 1> network_definitions = {
        {"mondrian_init.pb", "mondrian_predict.pb"}
};

namespace {

caffe2::NetBase* net;
caffe2::Workspace workspace;
const size_t kNumberOfChannels = 4; // RGBA/BGRA

float clamp(const float value) {
    if (value > 255.0f) {
        return 255.0f;
    } else if (value < 0.0f) {
        return 0.0f;
    } else {
        return value;
    }
}

caffe2::TensorCPU
YUVtoRGBAImage(const jbyte *Y, const jbyte *U, const jbyte *V, int height, int width,
               int UVRowStride) {
    caffe2::TensorCPU image_tensor;
    // The stylizer op expects HWC.
    image_tensor.Resize(1, height, width, kNumberOfChannels);
    float* image_data = image_tensor.mutable_data<float>();

    // https://en.wikipedia.org/wiki/YUV#Y%E2%80%B2UV420sp_(NV21)_to_RGB_conversion_(Android)

    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            // Tested on Pixel and S7.
            const auto y = static_cast<uint8_t>(Y[row * width + column]);
            const auto u = static_cast<uint8_t>(U[(row / 2) * (UVRowStride / 2) + column]);
            const auto v = static_cast<uint8_t>(V[(row / 2) * (UVRowStride / 2) + column]);

            // channels last
//            const auto b = (column * width) + (row * kNumberOfChannels) + 0;
//            const auto g = (column * width) + (row * kNumberOfChannels) + 1;
//            const auto r = (column * width) + (row * kNumberOfChannels) + 2;
//            const auto a = (column * width) + (row * kNumberOfChannels) + 3;

            // column major
//            const auto b = (0 * (height * width)) + (column * width) + row;
//            const auto g = (1 * (height * width)) + (column * width) + row;
//            const auto r = (2 * (height * width)) + (column * width) + row;
//            const auto a = (3 * (height * width)) + (column * width) + row;

            // row major
            const auto b = (0 * (height * width)) + (row * width) + column;
            const auto g = (1 * (height * width)) + (row * width) + column;
            const auto r = (2 * (height * width)) + (row * width) + column;
            const auto a = (3 * (height * width)) + (row * width) + column;

            // B = Y + 1.772 * (U - V)
            image_data[b] = clamp(y + 1.732446f * (u - v));

//            __android_log_print(ANDROID_LOG_INFO, "StyleTransfer", "b = %.5f", image_data[b]);

            // G = Y - 0.34414 * (U - 128) - 0.71414 * (V - 128)
            image_data[g] = clamp(y - 0.337633f * (u - 128.0f) - 0.698001f * (v - 128.0f));

//            __android_log_print(ANDROID_LOG_INFO, "StyleTransfer", "g = %.5f", image_data[g]);

            // R = Y + 1.402 * (V - 128)
            image_data[r] = clamp(y + 1.370705f * (v - 128.0f));

//            __android_log_print(ANDROID_LOG_INFO, "StyleTransfer", "r = %.5f", image_data[r]);

            // A = full opacity
            image_data[a] = 255.0f;
        }
    }

    return image_tensor;
}

std::vector<int> packRGBAPixels(const caffe2::TensorCPU& tensor, int height, int width) {
    std::vector<int> packed;
    packed.reserve(width * height);

    const uint8_t* data = tensor.data<uint8_t>();

    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            // row major
            const auto b = (row * width * 4) + (column * 4) + 0;
            const auto g = (row * width * 4) + (column * 4) + 1;
            const auto r = (row * width * 4) + (column * 4) + 2;

            // column major
//            const auto b = (0 * (height * width)) + (column * width) + row;
//            const auto g = (1 * (height * width)) + (column * width) + row;
//            const auto r = (2 * (height * width)) + (column * width) + row;

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


caffe2::NetDef loadNet(AAssetManager* asset_manager, const char* filename) {
    AAsset* asset = AAssetManager_open(asset_manager, filename, AASSET_MODE_BUFFER);
    const void* data = AAsset_getBuffer(asset);
    const off_t length = AAsset_getLength(asset);

    caffe2::NetDef net;
    if (net.ParseFromArray(data, length)) {
        ANDROID_LOG(ERROR, "Successfully loaded NetDef %s", filename);
    } else {
        ANDROID_LOG(ERROR, "Couldn't parse net from data.");
    }

    AAsset_close(asset);

    return net;
}


caffe2::TensorCPU transformImage(caffe2::TensorCPU& image_tensor) {
    auto* tensor = workspace.CreateBlob("data_int8_bgra")->GetMutable<caffe2::TensorCPU>();
    tensor->swap(image_tensor);

    net->Run();

    return workspace.GetBlob("styled_int8_bgra")->Get<caffe2::TensorCPU>();
}

jintArray toIntArray(JNIEnv *env, const std::vector<int>& output_buffer) {
    jintArray output_array = env->NewIntArray(output_buffer.size());
    if (output_array != nullptr) {
        env->SetIntArrayRegion(output_array, 0, output_buffer.size(), output_buffer.data());
    }

    return output_array;
}

} // namespace

extern "C"
void
Java_facebook_styletransfer_StyleTransfer_initCaffe2(
        JNIEnv *env,
        jobject /* this */,
        jobject jAssetManager) {
    AAssetManager* asset_manager = AAssetManager_fromJava(env, jAssetManager);

    ANDROID_LOG(INFO, "Attempting to load protobuf NetDefs...");
    const auto init_net = loadNet(asset_manager, "crayon_init.pb");
    const auto predict_net = loadNet(asset_manager, "crayon_predict.pb");

    ANDROID_LOG(INFO, "Running initialization network...");
    workspace.RunNetOnce(init_net);

    ANDROID_LOG(INFO, "Converting to OpenGL...");
    caffe2::NetDef gl_net_def;
    CAFFE_ENFORCE(caffe2::tryConvertToOpenGL(init_net, predict_net, &gl_net_def));

    ANDROID_LOG(INFO, "Creating Network...");
    net = workspace.CreateNet(gl_net_def);
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

    const jbyte *Y = env->GetByteArrayElements(YArray, nullptr);
    const jbyte *U = env->GetByteArrayElements(UArray, nullptr);
    const jbyte *V = env->GetByteArrayElements(VArray, nullptr);

    caffe2::TensorCPU image_tensor = YUVtoRGBAImage(Y, U, V, height, width, UVRowStride);
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


    const caffe2::TensorCPU output_tensor = transformImage(image_tensor);
    const std::vector<int> output_buffer = packRGBAPixels(output_tensor, height, width);

    return toIntArray(env, output_buffer);
}
