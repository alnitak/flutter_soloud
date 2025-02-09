package flutter.soloud.flutter_soloud;

import androidx.annotation.NonNull;
import io.flutter.embedding.engine.plugins.FlutterPlugin;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.plugin.common.MethodChannel.MethodCallHandler;
import io.flutter.plugin.common.MethodChannel.Result;
import android.media.AudioManager;
import android.util.Log;

public class FlutterSoloudPlugin implements FlutterPlugin, MethodCallHandler, AudioManager.OnAudioFocusChangeListener {
    private static MethodChannel channel;
    private static final String CHANNEL_NAME = "flutter_soloud";
    private static boolean isInitialized = false;

    static {
        System.loadLibrary("flutter_soloud_plugin");
    }

    // Native method declarations
    private native void nativeOnAudioFocusChange(int focusChange);

    @Override
    public void onAudioFocusChange(int focusChange) {
        nativeOnAudioFocusChange(focusChange);
    }

    @Override
    public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
        if (channel == null) {
            channel = new MethodChannel(binding.getBinaryMessenger(), CHANNEL_NAME);
            channel.setMethodCallHandler(this);
        }
    }

    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull Result result) {
        if (call.method.equals("initialize")) {
            if (!isInitialized) {
                Log.d("FlutterSoloudPlugin", "*****************initialize called");
                isInitialized = true;
            }
            result.success(true);
        } else {
            result.notImplemented();
        }
    }

    @Override
    public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
        if (channel != null) {
            channel.setMethodCallHandler(null);
            channel = null;
        }
        isInitialized = false;
    }
}
