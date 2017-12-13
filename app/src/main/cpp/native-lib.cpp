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

struct NetworkDefinition {
    std::string init_net;
    std::string predict_net;
};

const std::array<NetworkDefinition, 1> network_definitions = {
        {"mondrian_init.pb", "mondrian_predict.pb"}
};

namespace {

// Constants
const size_t kNumberOfChannels = 4; // RGBA/BGRA

// Global variables
caffe2::NetDef init_net;
caffe2::NetDef predict_net;
caffe2::NetBase* net = nullptr;
caffe2::Workspace workspace;
static caffe2::Predictor *_predictor;
static caffe2::GLPredictor* openGLPredictor;

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


void loadNet(AAssetManager* asset_manager, const char* filename, caffe2::NetDef* net) {
    AAsset* asset = AAssetManager_open(asset_manager, filename, AASSET_MODE_BUFFER);
    const void* data = AAsset_getBuffer(asset);
    const off_t length = AAsset_getLength(asset);

    if (net->ParseFromArray(data, length)) {
        ANDROID_LOG(INFO, "Successfully loaded NetDef %s", filename);
    } else {
        ANDROID_LOG(ERROR, "Couldn't parse net from data.");
    }

    AAsset_close(asset);
}


caffe2::TensorCPU* transformImage(caffe2::TensorCPU& image_tensor) {
    std::vector<caffe2::TensorCPU*> output;
    _predictor->run({&image_tensor}, &output);
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

    ANDROID_LOG(INFO, "Attempting to load protobuf NetDefs...");
    loadNet(asset_manager, "crayon_init.pb", &init_net);
    loadNet(asset_manager, "mondrian_predict.pb", &predict_net);

    ANDROID_LOG(INFO, "Running initialization network...");
//    CAFFE_ENFORCE(workspace.RunNetOnce(init_net));

    try {
        openGLPredictor = new caffe2::GLPredictor(init_net, predict_net);
        ANDROID_LOG(ERROR, "StyleTransfer asdfsdfasdf: %s", openGLPredictor->def().DebugString().c_str());
    } catch(...) {
        ANDROID_LOG(INFO, "BAD$$$$$$$$$$$$$$$$$$$$$$");
    }

    try {
        ANDROID_LOG(ERROR, "StyleTransfer Instantiating predictor...");
        _predictor = new caffe2::Predictor(init_net, predict_net);
        assert(_predictor != nullptr);
        ANDROID_LOG(ERROR, "StyleTransfer done.")
    } catch(...) {
        ANDROID_LOG(INFO, "StyleTransfer ????????????????????????");
    }

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
    const caffe2::TensorCPU* output_tensor = transformImage(image_tensor);

    ANDROID_LOG(INFO, "Packing RGB pixels...");
    const std::vector<int> output_buffer = packRGBAPixels(*output_tensor, height, width);

    ANDROID_LOG(INFO, "Copying to output...");
    return toIntArray(env, output_buffer);
}

//#define IMG_H 227
//#define IMG_W 227
//#define IMG_C 3
//#define MAX_DATA_SIZE IMG_H * IMG_W * IMG_C
//#define alog(...) __android_log_print(ANDROID_LOG_ERROR, "F8DEMO", __VA_ARGS__);
//
//static caffe2::NetDef _initNet, _predictNet;
//static caffe2::Predictor *_predictor;
//static caffe2::GLPredictor* openGLPredictor;
//static char raw_data[MAX_DATA_SIZE];
//static float input_data[MAX_DATA_SIZE];
//static caffe2::Workspace ws;
//static caffe2::NetBase* net;
//
//void loadNet(AAssetManager* asset_manager, const char* filename, caffe2::NetDef* net) {
//    AAsset* asset = AAssetManager_open(asset_manager, filename, AASSET_MODE_BUFFER);
//    const void* data = AAsset_getBuffer(asset);
//    const off_t length = AAsset_getLength(asset);
//
//    if (net->ParseFromArray(data, length)) {
//        ANDROID_LOG(INFO, "Successfully loaded NetDef %s", filename);
//    } else {
//        ANDROID_LOG(ERROR, "Couldn't parse net from data.");
//    }
//
//    AAsset_close(asset);
//}
//
//// A function to load the NetDefs from protobufs.
//void loadToNetDef(AAssetManager* mgr, caffe2::NetDef* net, const char *filename) {
//    AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_BUFFER);
//    assert(asset != nullptr);
//    const void *data = AAsset_getBuffer(asset);
//    assert(data != nullptr);
//    off_t len = AAsset_getLength(asset);
//    assert(len != 0);
//    if (!net->ParseFromArray(data, len)) {
//        alog("Couldn't parse net from data.\n");
//    }
//    AAsset_close(asset);
//}
//
//extern "C"
//void
//Java_facebook_styletransfer_StyleTransfer_initCaffe2(
//        JNIEnv* env,
//        jobject /* this */,
//        jobject jAssetManager) {
//    AAssetManager* asset_manager = AAssetManager_fromJava(env, jAssetManager);
//
//    ANDROID_LOG(INFO, "Attempting to load protobuf NetDefs...");
//    loadNet(asset_manager, "mondrian_init.pb", &_initNet);
//    loadNet(asset_manager, "mondrian_predict.pb", &_predictNet);
//
//    ANDROID_LOG(INFO, "StyleTransfer Converting to OpenGL network...");
////    caffe2::NetDef gl_net_def;
////    CAFFE_ENFORCE(caffe2::tryConvertToOpenGL(_initNet, _predictNet, &gl_net_def));
//
////    ANDROID_LOG(INFO, "Running initialization network...");
////    CAFFE_ENFORCE(ws.RunNetOnce(_initNet));
////
////    ANDROID_LOG(INFO, "Creating Network...");
////    net = ws.CreateNet(_predictNet);
////    CAFFE_ENFORCE(net != nullptr);
////
////    ANDROID_LOG(INFO, "Initialization complete!");
//
//    try {
//        openGLPredictor = new caffe2::GLPredictor(_initNet, _predictNet);
//        ANDROID_LOG(ERROR, "StyleTransfer asdfsdfasdf: %s", openGLPredictor->def().DebugString().c_str());
//    } catch(...) {
//        ANDROID_LOG(INFO, "BAD$$$$$$$$$$$$$$$$$$$$$$");
//    }
//
//    try {
//        ANDROID_LOG(ERROR, "StyleTransfer Instantiating predictor...");
//        _predictor = new caffe2::Predictor(_initNet, _predictNet);
//        assert(_predictor != nullptr);
//        ANDROID_LOG(ERROR, "StyleTransfer done.")
//    } catch(...) {
//        ANDROID_LOG(INFO, "StyleTransfer ????????????????????????");
//    }
//}
//
////extern "C"
////void
////Java_facebook_styletransfer_StyleTransfer_initCaffe2(
////        JNIEnv *env,
////        jobject /* this */,
////        jobject jAssetManager) {
////    AAssetManager* asset_manager = AAssetManager_fromJava(env, jAssetManager);
////
////    ANDROID_LOG(INFO, "Attempting to load protobuf NetDefs...");
////    loadNet(asset_manager, "crayon_init.pb", &init_net);
////    loadNet(asset_manager, "mondrian_predict.pb", &predict_net);
////
////    ANDROID_LOG(INFO, "Running initialization network...");
//////    CAFFE_ENFORCE(workspace.RunNetOnce(init_net));
////
////    ANDROID_LOG(INFO, "Converting to OpenGL network...");
////    caffe2::NetDef gl_net_def;
////    CAFFE_ENFORCE(caffe2::tryConvertToOpenGL(init_net, predict_net, &gl_net_def));
////
////    ANDROID_LOG(INFO, "Creating Network...");
////    net = workspace.CreateNet(gl_net_def);
////    CAFFE_ENFORCE(net != nullptr);
////
////    ANDROID_LOG(INFO, "Initialization complete!");
////}
////
//
//float avg_fps = 0.0;
//float total_fps = 0.0;
//int iters_fps = 10;
//
//extern "C"
//JNIEXPORT jintArray JNICALL
//Java_facebook_styletransfer_StyleTransfer_transformImageWithCaffe2(
//        JNIEnv *env,
//        jobject /* this */,
//        jint styleIndex,
//        jint h,
//        jint w,
//        jbyteArray Y,
//        jbyteArray U,
//        jbyteArray V,
//        jint rowStride,
//        jint pixelStride) {
//    const bool infer_HWC = false;
//    if (!_predictor) {
//        ANDROID_LOG(ERROR, "style Style !!!!!!!!!!!!!");
//        return nullptr;
//    }
//    jsize Y_len = env->GetArrayLength(Y);
//    jbyte * Y_data = env->GetByteArrayElements(Y, 0);
//    jsize U_len = env->GetArrayLength(U);
//    jbyte * U_data = env->GetByteArrayElements(U, 0);
//    jsize V_len = env->GetArrayLength(V);
//    jbyte * V_data = env->GetByteArrayElements(V, 0);
//
//#define min(a,b) ((a) > (b)) ? (b) : (a)
//#define max(a,b) ((a) > (b)) ? (a) : (b)
//
//    auto h_offset = max(0, (h - IMG_H) / 2);
//    auto w_offset = max(0, (w - IMG_W) / 2);
//
//    auto iter_h = IMG_H;
//    auto iter_w = IMG_W;
//    if (h < IMG_H) {
//        iter_h = h;
//    }
//    if (w < IMG_W) {
//        iter_w = w;
//    }
//
//    for (auto i = 0; i < iter_h; ++i) {
//        jbyte* Y_row = &Y_data[(h_offset + i) * w];
//        jbyte* U_row = &U_data[(h_offset + i) / 4 * rowStride];
//        jbyte* V_row = &V_data[(h_offset + i) / 4 * rowStride];
//        for (auto j = 0; j < iter_w; ++j) {
//            // Tested on Pixel and S7.
//            char y = Y_row[w_offset + j];
//            char u = U_row[pixelStride * ((w_offset+j)/pixelStride)];
//            char v = V_row[pixelStride * ((w_offset+j)/pixelStride)];
//
//            float b_mean = 104.00698793f;
//            float g_mean = 116.66876762f;
//            float r_mean = 122.67891434f;
//
//            auto b_i = 0 * IMG_H * IMG_W + j * IMG_W + i;
//            auto g_i = 1 * IMG_H * IMG_W + j * IMG_W + i;
//            auto r_i = 2 * IMG_H * IMG_W + j * IMG_W + i;
//
//            if (infer_HWC) {
//                b_i = (j * IMG_W + i) * IMG_C;
//                g_i = (j * IMG_W + i) * IMG_C + 1;
//                r_i = (j * IMG_W + i) * IMG_C + 2;
//            }
///*
//  R = Y + 1.402 (V-128)
//  G = Y - 0.34414 (U-128) - 0.71414 (V-128)
//  B = Y + 1.772 (U-V)
// */
//            input_data[r_i] = -r_mean + (float) ((float) min(255., max(0., (float) (y + 1.402 * (v - 128)))));
//            input_data[g_i] = -g_mean + (float) ((float) min(255., max(0., (float) (y - 0.34414 * (u - 128) - 0.71414 * (v - 128)))));
//            input_data[b_i] = -b_mean + (float) ((float) min(255., max(0., (float) (y + 1.772 * (u - v)))));
//
//        }
//    }
//
//    caffe2::TensorCPU input;
//    if (infer_HWC) {
//        input.Resize(std::vector<int>({IMG_H, IMG_W, IMG_C}));
//    } else {
//        input.Resize(std::vector<int>({1, IMG_C, IMG_H, IMG_W}));
//    }
//    memcpy(input.mutable_data<float>(), input_data, IMG_H * IMG_W * IMG_C * sizeof(float));
//    caffe2::Predictor::TensorVector input_vec{&input};
//    caffe2::Predictor::TensorVector output_vec;
//    caffe2::Timer t;
//    t.Start();
//    ANDROID_LOG(ERROR, "StyleTransfer Transforms~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
//
//
////    auto* tensor = ws.CreateBlob("data")->GetMutable<caffe2::TensorCPU>();
////    tensor->swap(input);
////
////    CAFFE_ENFORCE(net != nullptr);
////    net->Run();
////
////    return ws.GetBlob("softmaxout")->Get<caffe2::TensorCPU>();
//
////    caffe2::Predictor* p = openGLPredictor;
////    p->run(input_vec, &output_vec);
////    openGLPredictor->run(input_vec, &output_vec);
//    _predictor->run(input_vec, &output_vec);
//
//    ANDROID_LOG(ERROR, "StyleTransfer Boom~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
//
//    float fps = 1000/t.MilliSeconds();
//    total_fps += fps;
//    avg_fps = total_fps / iters_fps;
//    total_fps -= avg_fps;
//
//    return nullptr;
//}