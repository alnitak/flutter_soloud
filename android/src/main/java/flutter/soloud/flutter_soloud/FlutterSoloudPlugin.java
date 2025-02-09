package flutter.soloud.flutter_soloud;

import androidx.annotation.NonNull;
import io.flutter.embedding.engine.plugins.FlutterPlugin;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.plugin.common.MethodChannel.MethodCallHandler;
import io.flutter.plugin.common.MethodChannel.Result;
import android.media.AudioManager;
import android.util.Log;
import android.content.Context;

/// Ref: https://developer.android.com/media/optimize/audio-focus
public class FlutterSoloudPlugin implements FlutterPlugin, MethodCallHandler, AudioManager.OnAudioFocusChangeListener {
    private static MethodChannel channel;
    private static final String CHANNEL_NAME = "flutter_soloud";
    private static boolean isInitialized = false;
    private AudioManager audioManager;
    private Context context;

    static {
        System.loadLibrary("flutter_soloud_plugin");
    }

    // Native method declarations
    private native void nativeOnAudioFocusChange(int focusChange);

    @Override
    public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
        if (channel == null) {
            context = binding.getApplicationContext();
            audioManager = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);
            channel = new MethodChannel(binding.getBinaryMessenger(), CHANNEL_NAME);
            channel.setMethodCallHandler(this);
        }
    }

    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull Result result) {
        if (call.method.equals("initialize")) {
            if (!isInitialized) {
                Log.d("FlutterSoloudPlugin.java", "*****************initialize called");
                requestAudioFocus();
                isInitialized = true;
            }
            result.success(true);
        } else {
            result.notImplemented();
        }
    }

    private void requestAudioFocus() {
        if (audioManager != null) {
            int result = audioManager.requestAudioFocus(this,
                    AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN);
            
            if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                Log.d("FlutterSoloudPlugin", "Audio focus request granted");
            } else {
                Log.d("FlutterSoloudPlugin", "Audio focus request failed");
            }
        }
    }

    @Override
    public void onAudioFocusChange(int focusChange) {
        Log.d("FlutterSoloudPlugin", "Audio focus changed: " + focusChange);
        nativeOnAudioFocusChange(focusChange);
    }

    @Override
    public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
        if (audioManager != null) {
            audioManager.abandonAudioFocus(this);
        }
        if (channel != null) {
            channel.setMethodCallHandler(null);
            channel = null;
        }
        audioManager = null;
        context = null;
        isInitialized = false;
    }
}
