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
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;

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
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                AudioAttributes playbackAttributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .setFlags(AudioAttributes.FLAG_AUDIBILITY_ENFORCED)
                    .build();

                AudioFocusRequest focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(playbackAttributes)
                    .setAcceptsDelayedFocusGain(true)
                    .setWillPauseWhenDucked(true)
                    .setOnAudioFocusChangeListener(this)
                    .build();

                int result = audioManager.requestAudioFocus(focusRequest);
                Log.d("FlutterSoloudPlugin", "Requesting audio focus with new attributes");
                if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                    Log.d("FlutterSoloudPlugin", "Audio focus request granted");
                } else {
                    Log.d("FlutterSoloudPlugin", "Audio focus request failed");
                }
            } else {
                // For older Android versions, we need to specify flags
                int result = audioManager.requestAudioFocus(this,
                        AudioManager.STREAM_MUSIC,
                        AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK);
                
                if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                    Log.d("FlutterSoloudPlugin", "Audio focus request granted");
                } else {
                    Log.d("FlutterSoloudPlugin", "Audio focus request failed");
                }
            }
        }
    }

    @Override
    public void onAudioFocusChange(int focusChange) {
        Log.d("FlutterSoloudPlugin", "Audio focus changed: " + focusChange);
        
        String focusState;
        switch (focusChange) {
            case AudioManager.AUDIOFOCUS_GAIN:
                focusState = "AUDIOFOCUS_GAIN";
                break;
            case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT:
                focusState = "AUDIOFOCUS_GAIN_TRANSIENT";
                break;
            case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE:
                focusState = "AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE";
                break;
            case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK:
                focusState = "AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK";
                break;
            case AudioManager.AUDIOFOCUS_LOSS:
                focusState = "AUDIOFOCUS_LOSS";
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                focusState = "AUDIOFOCUS_LOSS_TRANSIENT";
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                focusState = "AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK";
                break;
            case AudioManager.AUDIOFOCUS_NONE:
                focusState = "AUDIOFOCUS_NONE";
                break;
            default:
                focusState = "UNKNOWN";
                break;
        }

        if (channel != null) {
            channel.invokeMethod("onAudioFocusChanged", focusState);
        }
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
