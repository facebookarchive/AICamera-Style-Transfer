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

// Android includes
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#define ANDROID_LOG(level, ...) __android_log_print(ANDROID_LOG_##level, "styletransfer", __VA_ARGS__);

struct NetworkDefinition {
    std::string init_net;
    std::string predict_net;
};

const std::array<NetworkDefinition, 1> network_definitions = {
        {"mondrian_init.pb", "mondrian_predict.pb"}
};


// vector<Predictor>

namespace {

caffe2::NetDef _initNet, _predictNet;
caffe2::Predictor *_predictor;
caffe2::Workspace ws;
float avg_fps = 0.0;
float total_fps = 0.0;
int iters_fps = 10;

std::vector<float>
YUVtoRGBImage(const jbyte *Y, const jbyte *U, const jbyte *V, int height, int width, int rowStride,
           int pixelStride) {

    std::vector<float> image(height * width * 3);

    for (auto i = 0; i < height; ++i) {
        const jbyte *Y_row = &Y[i * width];
        const jbyte *U_row = &U[i / 4 * rowStride];
        const jbyte *V_row = &V[i / 4 * rowStride];
        for (auto j = 0; j < width; ++j) {
            // Tested on Pixel and S7.
            char y = Y_row[j];
            char u = U_row[pixelStride * (j / pixelStride)];
            char v = V_row[pixelStride * (j / pixelStride)];

            float b_mean = 104.00698793f;
            float g_mean = 116.66876762f;
            float r_mean = 122.67891434f;

            auto b_i = 0 * height * width + j * width + i;
            auto g_i = 1 * height * width + j * width + i;
            auto r_i = 2 * height * width + j * width + i;

/*
  R = Y + 1.402 (V-128)
  G = Y - 0.34414 (U-128) - 0.71414 (V-128)
  B = Y + 1.772 (U-V)
 */
            image[r_i] = -r_mean + (float) ((float) std::min<float>(255., std::max<float>(0.,
                                                                                          (float) (
                                                                                                  y +
                                                                                                  1.402 *
                                                                                                  (v -
                                                                                                   128)))));
            image[g_i] = -g_mean + (float) ((float) std::min<float>(255., std::max<float>(0.,
                                                                                          (float) (
                                                                                                  y -
                                                                                                  0.34414 *
                                                                                                  (u -
                                                                                                   128) -
                                                                                                  0.71414 *
                                                                                                  (v -
                                                                                                   128)))));
            image[b_i] = -b_mean + (float) ((float) std::min<float>(255., std::max<float>(0.,
                                                                                          (float) (
                                                                                                  y +
                                                                                                  1.772 *
                                                                                                  (u -
                                                                                                   v)))));

        }
    }

    return image;
}

jbyte* RGBtoYUVImage(const caffe2::TensorCPU& image_tensor) {
    jbyte* image_buffer = nullptr;
    return image_buffer;
}


// A function to load the NetDefs from protobufs.
void loadToNetDef(AAssetManager *mgr, caffe2::NetDef *net, const char *filename) {
    AAsset *asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
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


caffe2::TensorCPU transformImage(caffe2::TensorCPU& image_tensor) {
    caffe2::Predictor::TensorVector input_vec{&image_tensor};
    caffe2::Predictor::TensorVector output_vec;

    caffe2::Timer t;
    t.Start();
    _predictor->run(input_vec, &output_vec);
    float fps = 1000 / t.MilliSeconds();
    total_fps += fps;
    avg_fps = total_fps / iters_fps;
    total_fps -= avg_fps;

    if (output_vec.size() == 0) {
        ANDROID_LOG(ERROR, "== 0");
    } else if (output_vec.size() == 1) {
        ANDROID_LOG(ERROR, "== 1");
    } else {
        ANDROID_LOG(ERROR, "> 1");
    }

    return image_tensor;
}

} // namespace

extern "C"
void
Java_facebook_styletransfer_StyleTransfer_initCaffe2(
        JNIEnv *env,
        jobject /* this */,
        jobject assetManager) {
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    ANDROID_LOG(INFO, "Attempting to load protobuf NetDefs...");
    loadToNetDef(mgr, &_initNet, "squeeze_init_net.pb");
    loadToNetDef(mgr, &_predictNet, "squeeze_predict_net.pb");
    ANDROID_LOG(INFO, "NetDefs loaded!");
    ANDROID_LOG(INFO, "Instantiating predictor...");
    _predictor = new caffe2::Predictor(_initNet, _predictNet);
    assert(_predictor);
    ANDROID_LOG(INFO, "Predictor insantiated!");

}

extern "C"
JNIEXPORT jintArray JNICALL
Java_facebook_styletransfer_StyleTransfer_transformImageWithCaffe2(
        JNIEnv *env,
        jobject /* this */,
        jint styleIndex,
        jint height,
        jint width,
        jbyteArray Y,
        jbyteArray U,
        jbyteArray V,
        jint rowStride,
        jint pixelStride) {

    if (!_predictor) {
        ANDROID_LOG(ERROR, "Predictor was null");
        return nullptr;
    }

    const jbyte *Y_data = env->GetByteArrayElements(Y, 0);
    const jbyte *U_data = env->GetByteArrayElements(U, 0);
    const jbyte *V_data = env->GetByteArrayElements(V, 0);

//    const auto image = YUVtoRGBImage(Y_data, U_data, V_data, height, width, rowStride, pixelStride);
//    assert(image.size() == width * height * 3);

//    caffe2::TensorCPU image_tensor;
//    image_tensor.Resize(std::vector<int>({1, IMG_C, height, width}));
//    memcpy(image_tensor.mutable_data<float>(), image.data(), height * width * IMG_C * sizeof(float));
//
//
//    const caffe2::TensorCPU output_tensor = transformImage(image_tensor);
//    const jbyte* output_buffer = RGBtoYUVImage(image_tensor);


//    std::vector<int> output_buffer(image.size());
//    std::copy(image.begin(), image.end(), output_buffer.begin());

    return nullptr;
//    jintArray output_array = env->NewIntArray(output_buffer.size());
//    if (output_array != nullptr) {
//        env->SetIntArrayRegion(output_array, 0, output_buffer.size(), output_buffer.data());
//    }
//
//    return output_array;
}
