package facebook.styletransfer;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.ImageFormat;
import android.graphics.Matrix;
import android.graphics.SurfaceTexture;
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
import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.annotation.NonNull;
import android.util.Log;
import android.util.Size;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.Window;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ViewSwitcher;

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
            return true;
        }

        @Override
        public boolean onFling(MotionEvent e1, MotionEvent e2, float velocityX, float velocityY) {
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
            int UVRowStride,
            int UVPixelStride);

    public native void initCaffe2(AssetManager mgr);

    private final class SetUpNeuralNetwork extends AsyncTask<Void, Void, Void> {
        @Override
        protected Void doInBackground(Void[] v) {
            try {
                initCaffe2(mAssetManager);
            } catch (Exception e) {
                Log.e(TAG, "Couldn't load neural network.");
            }
            return null;
        }
    }

    private class OnImageAvailableCallback implements ImageReader.OnImageAvailableListener {

        private byte[] getChannel(Image image, int index) {
            final ByteBuffer buffer = image.getPlanes()[index].getBuffer();
            byte[] array = new byte[buffer.capacity()];
            buffer.get(array);
            return array;
        }

        private void displayImage(int[] pixels, int height, int width) {
            final Bitmap bitmap = Bitmap.createBitmap(
                    pixels,
                    width,
                    height,
                    Bitmap.Config.ARGB_8888
            );

            final Matrix transform = new Matrix();
            transform.setRotate(90);
            final Bitmap rotated =
                    Bitmap.createBitmap(bitmap, 0, 0, width, height, transform, false);

            mImageView.setImageBitmap(rotated);
        }

        @Override
        public void onImageAvailable(ImageReader reader) {
            mImage = null;
            try {
                mImage = reader.acquireLatestImage();

                if (mImage == null) {
                    Log.w(TAG, "Acquired image was null");
                    return;
                }
                if (mCurrentlyProcessing.get() || mStyleIndex == 0) {
                    mImage.close();
                    return;
                }

                mCurrentlyProcessing.set(true);

                final byte[] Y = getChannel(mImage, 0);
                final byte[] U = getChannel(mImage, 1);
                final byte[] V = getChannel(mImage, 2);

                final int UVRowStride = mImage.getPlanes()[1].getRowStride();
                final int UVPixelStride = mImage.getPlanes()[1].getPixelStride();

                final int height = mImage.getHeight();
                final int width = mImage.getWidth();

                mTransformedImage = transformImageWithCaffe2(
                        mStyleIndex - 1,
                        height,
                        width,
                        Y,
                        U,
                        V,
                        UVRowStride,
                        UVPixelStride);

                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (mTransformedImage != null) {
                            displayImage(mTransformedImage, height, width);
                            mTransformedImage = null;
                        }
                        mCurrentlyProcessing.set(false);
                    }
                });

            } finally {
                if (mImage != null) {
                    mImage.close();
                }
            }
        }
    }

    private static final String TAG = "StyleTransfer";
    private static final int REQUEST_CAMERA_PERMISSION = 200;
    private CameraDevice mCameraDevice;
    private static final String[] STYLES = {
            "Preview",
            "Night",
            "Flowers",
    };

    private CameraCaptureSession mCameraCaptureSession;
    private Size mPreviewSize;
    private TextureView mTextureView;
    private ImageView mImageView;
    private Handler mBackgroundHandler;
    private CaptureRequest.Builder mCaptureRequestBuilder;
    private HandlerThread mBackgroundThread;
    private int mStyleIndex = 1;
    private GestureDetector mGestureDetector;
    private ViewSwitcher mViewSwitcher;
    private AssetManager mAssetManager;
    private Image mImage = null;
    private int[] mTransformedImage;
    private ImageReader mImageReader;
    private TextView mTextView;
    private final AtomicBoolean mCurrentlyProcessing = new AtomicBoolean(false);
    private final TextureViewListener mTextureViewListener = new TextureViewListener();
    private final CameraStateCallback cameraStateCallback = new CameraStateCallback();
    private final OnImageAvailableCallback mOnImageAvailable = new OnImageAvailableCallback();


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Turn off the title at the top of the screen.
        requestWindowFeature(Window.FEATURE_NO_TITLE);

        final View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN & View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);

        setContentView(R.layout.activity_styletransfer);

        mViewSwitcher = (ViewSwitcher) findViewById(R.id.view_switcher);
        mTextView = (TextView) findViewById(R.id.textView);
        mImageView = (ImageView) findViewById(R.id.imageView);

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
            texture.setDefaultBufferSize(mPreviewSize.getWidth(), mPreviewSize.getHeight());
            final Surface surface = new Surface(texture);

            mImageReader = ImageReader.newInstance(
                    mPreviewSize.getWidth(),
                    mPreviewSize.getHeight(),
                    ImageFormat.YUV_420_888,
                    4
            );

            mImageReader.setOnImageAvailableListener(mOnImageAvailable, mBackgroundHandler);

            mCaptureRequestBuilder = mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            mCaptureRequestBuilder.addTarget(surface);
            mCaptureRequestBuilder.addTarget(mImageReader.getSurface());

            final List<Surface> surfaces = Arrays.asList(surface, mImageReader.getSurface());
            CameraCaptureSessionStateCallback callback = new CameraCaptureSessionStateCallback();
            mCameraDevice.createCaptureSession(surfaces, callback, null);


        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    private void openCamera() {
        final CameraManager cameraManager = (CameraManager) getSystemService(Context.CAMERA_SERVICE);
        try {
            final String mCameraId = cameraManager.getCameraIdList()[0];
            final CameraCharacteristics characteristics = cameraManager.getCameraCharacteristics(mCameraId);
            final StreamConfigurationMap map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);

            final Size[] previewSizes = map.getOutputSizes(SurfaceTexture.class);
            mPreviewSize = previewSizes[previewSizes.length - 1];
            for (Size size : previewSizes) {
                if (size.getWidth() < 500 && size.getHeight() < 500) {
                    mPreviewSize = size;
                    break;
                }
            }
            Log.i(TAG, "Using preview size: " + mPreviewSize.toString());

            final int cameraPermission = checkSelfPermission(Manifest.permission.CAMERA);
            final int writePermission = checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE);
            if (cameraPermission == PERMISSION_GRANTED && writePermission == PERMISSION_GRANTED) {
                cameraManager.openCamera(mCameraId, cameraStateCallback, null);
            } else {
                Log.i(TAG, "Requesting permission");
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
