package facebook.styletransfer;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.graphics.YuvImage;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.media.Image;
import android.media.ImageReader;
import android.net.Uri;
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.annotation.NonNull;
import android.util.Log;
import android.util.Size;
import android.view.DragEvent;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.Window;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ViewSwitcher;

import org.w3c.dom.Text;

import java.io.ByteArrayOutputStream;
import java.net.URI;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

import static android.support.v4.content.PermissionChecker.PERMISSION_GRANTED;
import static android.view.View.SYSTEM_UI_FLAG_IMMERSIVE;

public class StyleTransfer extends Activity {
    private class CameraStateCallback extends CameraDevice.StateCallback {
        @Override
        public void onOpened(@NonNull CameraDevice camera) {
            mCameraDevice = camera;
            createCameraPreview();
        }

        @Override
        public void onDisconnected(@NonNull CameraDevice camera) {
            camera.close();
        }

        @Override
        public void onError(@NonNull CameraDevice camera, int error) {
            if (mCameraDevice != null) {
                mCameraDevice.close();
                mCameraDevice = null;
            }
        }
    }

    private class CameraCaptureSessionStateCallback extends CameraCaptureSession.StateCallback {
        @Override
        public void onConfigured(@NonNull CameraCaptureSession session) {
            if (mCameraDevice != null) {
                mCameraCaptureSession = session;
                updatePreview();
            }
        }

        @Override
        public void onConfigureFailed(@NonNull CameraCaptureSession session) {
            Toast toast = Toast.makeText(
                    getApplicationContext(),
                    "Configuration failed",
                    Toast.LENGTH_SHORT
            );
            toast.show();
        }

        private void updatePreview() {
            if (mCameraDevice != null) {
                mCaptureRequestBuilder.set(
                        CaptureRequest.CONTROL_MODE,
                        CameraMetadata.CONTROL_MODE_AUTO
                );
                try {
                    mCameraCaptureSession.setRepeatingRequest(
                            mCaptureRequestBuilder.build(),
                            null,
                            mBackgroundHandler
                    );
                } catch (CameraAccessException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    private class TextureViewListener implements TextureView.SurfaceTextureListener {
        @Override
        public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
            openCamera();
        }

        @Override
        public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {

        }

        @Override
        public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
            return true;
        }

        @Override
        public void onSurfaceTextureUpdated(SurfaceTexture surface) {

        }
    }

    private class GestureListener extends GestureDetector.SimpleOnGestureListener {
        @Override
        public boolean onDoubleTap(MotionEvent e) {
            mStyleIndex = 0;
            Log.d(TAG, "Double tap (reset)");
            return true;
        }

        @Override
        public boolean onFling(MotionEvent e1, MotionEvent e2, float velocityX, float velocityY) {
            if (velocityX > 0) {
                if (mStyleIndex > 0) {
                    if (mStyleIndex == 1) {
                        mViewSwitcher.showPrevious();
                    }
                    mStyleIndex -= 1;
                }
                Log.d(TAG, "Swipe right: " + mStyleIndex);
            } else if (velocityX < 0) {
                if (mStyleIndex < NUMBER_OF_STYLES - 1) {
                    if (mStyleIndex == 0) {
                        mViewSwitcher.showNext();
                    }
                    mStyleIndex += 1;
                }

                Log.d(TAG, "Swipe left: " + mStyleIndex);
            }
            return true;
        }

    }

    static {
        System.loadLibrary("native-lib");
    }

    public native int[] transformImageWithCaffe2(
            int mStyleIndex,
            int height,
            int width,
            byte[] Y,
            byte[] U,
            byte[] V,
            int rowStride,
            int pixelStride);

    public native void initCaffe2(AssetManager mgr);

    private final class SetUpNeuralNetwork extends AsyncTask<Void, Void, Void> {
        @Override
        protected Void doInBackground(Void[] v) {
            try {
                initCaffe2(mAssetManager);
            } catch (Exception e) {
                Log.d(TAG, "Couldn't load neural network.");
            }
            return null;
        }
    }

    private class OnImageAvailableCallback implements ImageReader.OnImageAvailableListener {

        private byte[] getChannel(Image image, int index) {
            final ByteBuffer buffer = image.getPlanes()[index].getBuffer();
            final byte[] array = new byte[buffer.capacity()];
            buffer.get(array);
            return array;
        }

        private void displayImage(int[] buffer, int height, int width) {

            final Bitmap bitmap = Bitmap.createBitmap(buffer, width, height, Bitmap.Config.RGB_565);

//            Bitmap.createBitmap(pix, picw, pich, Bitmap.Config.ARGB_8888)


//            final YuvImage image = new YuvImage(
//                    YUVBuffer,
//                    ImageFormat.YUV_420_888,
//                    height,
//                    width,
//                    null
//            );
//
//            final ByteArrayOutputStream stream = new ByteArrayOutputStream();
//            image.compressToJpeg(new Rect(0, 0, width, height), 50, stream);
//            final byte[] imageBytes = stream.toByteArray();
//            final Bitmap bitmap = BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.length);

            Log.d(TAG, "Displaying image");

            mImageView.setImageBitmap(bitmap);
        }

        private AtomicBoolean isProcessing = new AtomicBoolean(false);

        @Override
        public void onImageAvailable(ImageReader reader) {
            Image image = null;
            try {
                image = reader.acquireNextImage();

                if (isProcessing.get() || mStyleIndex == 0) {
                    image.close();
                    return;
                }

                isProcessing.set(true);

                Log.d(TAG, "style indeX: " + mStyleIndex);

                final byte[] Y = getChannel(image, 0);
                final byte[] U = getChannel(image, 1);
                final byte[] V = getChannel(image, 2);

                final int rowStride = image.getPlanes()[1].getRowStride();
                final int pixelStride = image.getPlanes()[1].getPixelStride();

                final int height = image.getHeight();
                final int width = image.getWidth();

                final int[] transformedImage = transformImageWithCaffe2(
                        mStyleIndex,
                        height,
                        width,
                        Y,
                        U,
                        V,
                        rowStride,
                        pixelStride);

                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (transformedImage != null) {
                            displayImage(transformedImage, height, width);
                        }
                        isProcessing.set(false);
                    }
                });

            } finally {
                if (image != null) {
                    image.close();
                }
            }
        }
    }

    private static final String TAG = "StyleTransfer";
    private static final int REQUEST_CAMERA_PERMISSION = 200;
    private static final int NUMBER_OF_STYLES = 3;

    private CameraDevice mCameraDevice;
    private CameraCaptureSession mCameraCaptureSession;
    private String mCameraId;
    private Size mPreviewSize;
    private TextureView mTextureView;
    private ImageView mImageView;
    private Handler mBackgroundHandler;
    private CaptureRequest.Builder mCaptureRequestBuilder;
    private HandlerThread mBackgroundThread;
    private int mStyleIndex = 0;
    private GestureDetector mGestureDetector;
    private ViewSwitcher mViewSwitcher;
    private AssetManager mAssetManager;
    private final TextureViewListener mTextureViewListener = new TextureViewListener();
    private final CameraStateCallback cameraStateCallback = new CameraStateCallback();


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Turn off the title at the top of the screen.
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        final View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);

        setContentView(R.layout.activity_styletransfer);

        mViewSwitcher = (ViewSwitcher)findViewById(R.id.view_switcher);

        mImageView = (ImageView) findViewById(R.id.imageView);
        mImageView.setImageResource(R.mipmap.cat);

        mTextureView = (TextureView) findViewById(R.id.textureView);
        mTextureView.setSystemUiVisibility(SYSTEM_UI_FLAG_IMMERSIVE);
        mTextureView.setSurfaceTextureListener(mTextureViewListener);

        mGestureDetector = new GestureDetector(getApplicationContext(), new GestureListener());

        mViewSwitcher.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                mGestureDetector.onTouchEvent(event);
                return true;
            }
        });

        mAssetManager = getResources().getAssets();

        new facebook.styletransfer.StyleTransfer.SetUpNeuralNetwork().execute();
    }

    protected void createCameraPreview() {
        try {
            final SurfaceTexture texture = mTextureView.getSurfaceTexture();
            assert texture != null;
            texture.setDefaultBufferSize(mPreviewSize.getWidth(), mPreviewSize.getHeight());
            final Surface surface = new Surface(texture);

            ImageReader imageReader = ImageReader.newInstance(
                    mPreviewSize.getWidth(),
                    mPreviewSize.getHeight(),
                    ImageFormat.YUV_420_888,
                    4
            );

            final OnImageAvailableCallback onImageAvailable = new OnImageAvailableCallback();
            imageReader.setOnImageAvailableListener(onImageAvailable, mBackgroundHandler);

            mCaptureRequestBuilder = mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            mCaptureRequestBuilder.addTarget(surface);
            mCaptureRequestBuilder.addTarget(imageReader.getSurface());

            final List<Surface> surfaces = Arrays.asList(surface, imageReader.getSurface());
            CameraCaptureSessionStateCallback callback = new CameraCaptureSessionStateCallback();
            mCameraDevice.createCaptureSession(surfaces, callback, null);


        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    private void openCamera() {
        final CameraManager cameraManager = (CameraManager) getSystemService(Context.CAMERA_SERVICE);
        try {
            mCameraId = cameraManager.getCameraIdList()[0];
            final CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(mCameraId);
            final StreamConfigurationMap map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
            mPreviewSize = map.getOutputSizes(SurfaceTexture.class)[0];
            final int cameraPermission = checkSelfPermission(Manifest.permission.CAMERA);
            final int writePermission = checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE);
            if (cameraPermission == PERMISSION_GRANTED && writePermission == PERMISSION_GRANTED) {
                cameraManager.openCamera(mCameraId, cameraStateCallback, null);
            } else {
                Log.d(TAG, "Requesting permission");
                final String[] permissions = {
                        Manifest.permission.CAMERA,
                        Manifest.permission.WRITE_EXTERNAL_STORAGE
                };
                requestPermissions(permissions, REQUEST_CAMERA_PERMISSION);
            }

        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        if (requestCode == REQUEST_CAMERA_PERMISSION) {
            if (grantResults[0] == PackageManager.PERMISSION_DENIED) {
                final Context context = getApplicationContext();
                final String message = "You can't use this app without granting permission";
                final Toast toast = Toast.makeText(context, message, Toast.LENGTH_LONG);
                toast.show();
                finish();
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        startBackgroundThread();
        if (mTextureView != null) {
            if (mTextureView.isAvailable()) {
                openCamera();
            } else {
                mTextureView.setSurfaceTextureListener(mTextureViewListener);
            }
        }
    }

    @Override
    protected void onPause() {
        if (mCameraDevice != null) {
            mCameraDevice.close();
            mCameraDevice = null;
        }
        stopBackgroundThread();
        super.onPause();
    }


    private void startBackgroundThread() {
        mBackgroundThread = new HandlerThread("Camera Background");
        mBackgroundThread.start();
        mBackgroundHandler = new Handler(mBackgroundThread.getLooper());
    }

    private void stopBackgroundThread() {
        mBackgroundThread.quitSafely();
        try {
            mBackgroundThread.join();
            mBackgroundThread = null;
            mBackgroundHandler = null;
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }
}

//public class StyleTransfer extends AppCompatActivity {
//
//    private static final String TAG = "F8DEMO";
//    private static final int REQUEST_CAMERA_PERMISSION = 200;
//
//    private TextureView textureView;
//    private String cameraId;
//    protected CameraDevice cameraDevice;
//    protected CameraCaptureSession cameraCaptureSessions;
//    protected CaptureRequest.Builder captureRequestBuilder;
//    private Size imageDimension;
//    private Handler mBackgroundHandler;
//    private HandlerThread mBackgroundThread;
//    private TextView tv;
//    private String predictedClass = "none";
//    private AssetManager mgr;
//    private boolean processing = false;
//    private Image image = null;
//    private boolean run_HWC = false;
//
//
//    static {
//        System.loadLibrary("native-lib");
//    }
//
//    public native String classificationFromCaffe2(int h, int w, byte[] Y, byte[] U, byte[] V,
//                                                  int rowStride, int pixelStride, boolean r_hwc);
//    public native void initCaffe2(AssetManager mgr);
//    private class SetUpNeuralNetwork extends AsyncTask<Void, Void, Void> {
//        @Override
//        protected Void doInBackground(Void[] v) {
//            try {
//                initCaffe2(mgr);
//                predictedClass = "Neural net loaded! Inferring...";
//            } catch (Exception e) {
//                Log.d(TAG, "Couldn't load neural network.");
//            }
//            return null;
//        }
//    }
//
//    @Override
//    protected void onCreate(Bundle savedInstanceState) {
//        super.onCreate(savedInstanceState);
//        this.requestWindowFeature(Window.FEATURE_NO_TITLE);
//
//        mgr = getResources().getAssets();
//
//        new facebook.styletransfer.StyleTransfer.SetUpNeuralNetwork().execute();
//
//        View decorView = getWindow().getDecorView();
//        int uiOptions = View.SYSTEM_UI_FLAG_FULLSCREEN;
//        decorView.setSystemUiVisibility(uiOptions);
//
//        setContentView(R.layout.activity_classify_camera);
//
//        textureView = (TextureView) findViewById(R.id.textureView);
//        textureView.setSystemUiVisibility(SYSTEM_UI_FLAG_IMMERSIVE);
//        final GestureDetector gestureDetector = new GestureDetector(this.getApplicationContext(),
//                new GestureDetector.SimpleOnGestureListener(){
//                    @Override
//                    public boolean onDoubleTap(MotionEvent e) {
//                        return true;
//                    }
//
//                    @Override
//                    public void onLongPress(MotionEvent e) {
//                        super.onLongPress(e);
//
//                    }
//
//                    @Override
//                    public boolean onDoubleTapEvent(MotionEvent e) {
//                        return true;
//                    }
//
//                    @Override
//                    public boolean onDown(MotionEvent e) {
//                        return true;
//                    }
//                });
//                });
//
//        textureView.setOnTouchListener(new View.OnTouchListener() {
//            @Override
//            public boolean onTouch(View v, MotionEvent event) {
//                return gestureDetector.onTouchEvent(event);
//            }
//        });
//
//        assert textureView != null;
//        textureView.setSurfaceTextureListener(textureListener);
//        tv = (TextView) findViewById(R.id.sample_text);
//
//    }
//
//    TextureView.SurfaceTextureListener textureListener = new TextureView.SurfaceTextureListener() {
//        @Override
//        public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
//            //open your camera here
//            openCamera();
//        }
//        @Override
//        public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
//            // Transform you image captured size according to the surface width and height
//        }
//        @Override
//        public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
//            return false;
//        }
//        @Override
//        public void onSurfaceTextureUpdated(SurfaceTexture surface) {
//        }
//    };
//    private final CameraDevice.StateCallback stateCallback = new CameraDevice.StateCallback() {
//        @Override
//        public void onOpened(CameraDevice camera) {
//            cameraDevice = camera;
//            createCameraPreview();
//        }
//        @Override
//        public void onDisconnected(CameraDevice camera) {
//            cameraDevice.close();
//        }
//        @Override
//        public void onError(CameraDevice camera, int error) {
//            cameraDevice.close();
//            cameraDevice = null;
//        }
//    };
//    protected void startBackgroundThread() {
//        mBackgroundThread = new HandlerThread("Camera Background");
//        mBackgroundThread.start();
//        mBackgroundHandler = new Handler(mBackgroundThread.getLooper());
//    }
//    protected void stopBackgroundThread() {
//        mBackgroundThread.quitSafely();
//        try {
//            mBackgroundThread.join();
//            mBackgroundThread = null;
//            mBackgroundHandler = null;
//        } catch (InterruptedException e) {
//            e.printStackTrace();
//        }
//    }
//
//    protected void createCameraPreview() {
//        try {
//            SurfaceTexture texture = textureView.getSurfaceTexture();
//            assert texture != null;
//            texture.setDefaultBufferSize(imageDimension.getWidth(), imageDimension.getHeight());
//            Surface surface = new Surface(texture);
//            int width = 227;
//            int height = 227;
//            ImageReader reader = ImageReader.newInstance(width, height, ImageFormat.YUV_420_888, 4);
//            ImageReader.OnImageAvailableListener readerListener = new ImageReader.OnImageAvailableListener() {
//                @Override
//                public void onImageAvailable(ImageReader reader) {
//                    try {
//
//                        image = reader.acquireNextImage();
//                        if (processing) {
//                            image.close();
//                            return;
//                        }
//                        processing = true;
//                        int w = image.getWidth();
//                        int h = image.getHeight();
//                        ByteBuffer Ybuffer = image.getPlanes()[0].getBuffer();
//                        ByteBuffer Ubuffer = image.getPlanes()[1].getBuffer();
//                        ByteBuffer Vbuffer = image.getPlanes()[2].getBuffer();
//                        // TODO: use these for proper image processing on different formats.
//                        int rowStride = image.getPlanes()[1].getRowStride();
//                        int pixelStride = image.getPlanes()[1].getPixelStride();
//                        byte[] Y = new byte[Ybuffer.capacity()];
//                        byte[] U = new byte[Ubuffer.capacity()];
//                        byte[] V = new byte[Vbuffer.capacity()];
//                        Ybuffer.get(Y);
//                        Ubuffer.get(U);
//                        Vbuffer.get(V);
//
//                        predictedClass = classificationFromCaffe2(h, w, Y, U, V,
//                                rowStride, pixelStride, run_HWC);
//                        runOnUiThread(new Runnable() {
//                            @Override
//                            public void run() {
//                                tv.setText(predictedClass);
//                                processing = false;
//                            }
//                        });
//
//                    } finally {
//                        if (image != null) {
//                            image.close();
//                        }
//                    }
//                }
//            };
//            reader.setOnImageAvailableListener(readerListener, mBackgroundHandler);
//            captureRequestBuilder = cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
//            captureRequestBuilder.addTarget(surface);
//            captureRequestBuilder.addTarget(reader.getSurface());
//
//            cameraDevice.createCaptureSession(Arrays.asList(surface, reader.getSurface()), new CameraCaptureSession.StateCallback(){
//                @Override
//                public void onConfigured(@NonNull CameraCaptureSession cameraCaptureSession) {
//                    if (null == cameraDevice) {
//                        return;
//                    }
//                    cameraCaptureSessions = cameraCaptureSession;
//                    updatePreview();
//                }
//                @Override
//                public void onConfigureFailed(@NonNull CameraCaptureSession cameraCaptureSession) {
//                    Toast.makeText(facebook.styletransfer.StyleTransfer.this, "Configuration change", Toast.LENGTH_SHORT).show();
//                }
//            }, null);
//        } catch (CameraAccessException e) {
//            e.printStackTrace();
//        }
//    }
//    private void openCamera() {
//        CameraManager manager = (CameraManager) getSystemService(Context.CAMERA_SERVICE);
//        try {
//            cameraId = manager.getCameraIdList()[0];
//            CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);
//            StreamConfigurationMap map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
//            assert map != null;
//            imageDimension = map.getOutputSizes(SurfaceTexture.class)[0];
//            if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED && ActivityCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
//                ActivityCompat.requestPermissions(facebook.styletransfer.StyleTransfer.this, new String[]{Manifest.permission.CAMERA, Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_CAMERA_PERMISSION);
//                return;
//            }
//            manager.openCamera(cameraId, stateCallback, null);
//        } catch (CameraAccessException e) {
//            e.printStackTrace();
//        }
//    }
//
//    protected void updatePreview() {
//        if(null == cameraDevice) {
//            return;
//        }
//        captureRequestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);
//        try {
//            cameraCaptureSessions.setRepeatingRequest(captureRequestBuilder.build(), null, mBackgroundHandler);
//        } catch (CameraAccessException e) {
//            e.printStackTrace();
//        }
//    }
//
//    private void closeCamera() {
//        if (null != cameraDevice) {
//            cameraDevice.close();
//            cameraDevice = null;
//        }
//    }
//    @Override
//    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
//        if (requestCode == REQUEST_CAMERA_PERMISSION) {
//            if (grantResults[0] == PackageManager.PERMISSION_DENIED) {
//                Toast.makeText(facebook.styletransfer.StyleTransfer.this, "You can't use this app without granting permission", Toast.LENGTH_LONG).show();
//                finish();
//            }
//        }
//    }
//
//    @Override
//    protected void onResume() {
//        super.onResume();
//        startBackgroundThread();
//        if (textureView.isAvailable()) {
//            openCamera();
//        } else {
//            textureView.setSurfaceTextureListener(textureListener);
//        }
//    }
//
//    @Override
//    protected void onPause() {
//        closeCamera();
//        stopBackgroundThread();
//        super.onPause();
//    }
//}