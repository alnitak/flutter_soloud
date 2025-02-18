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
import android.content.BroadcastReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.media.AudioDeviceInfo;
import android.media.MediaRouter;
import android.media.MediaRouter.RouteInfo;
import android.media.MediaRouter.RouteGroup;
import android.bluetooth.BluetoothDevice;
import java.util.Map;

/// Ref: https://developer.android.com/media/optimize/audio-focus
/// Ref: https://developer.android.com/reference/android/media/AudioManager#AUDIOFOCUS_GAIN
public class FlutterSoloudPlugin implements FlutterPlugin, MethodCallHandler, AudioManager.OnAudioFocusChangeListener {
    private static MethodChannel channel;
    private static final String CHANNEL_NAME = "flutter_soloud";
    private static boolean isInitialized = false;
    private AudioManager audioManager;
    private Context context;
    private BroadcastReceiver headsetPlugReceiver;

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

            // Register headset plug receiver
            registerHeadsetPlugReceiver();
        }
    }

    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull Result result) {
        if (call.method.equals("initialize")) {
            if (!isInitialized) {
                Map<String, Object> attributes = call.arguments();
                requestAudioFocus(attributes);
                isInitialized = true;
            }
            result.success(true);
        } else {
            result.notImplemented();
        }
    }

    private void requestAudioFocus(Map<String, Object> attributes) {
        if (audioManager != null) {
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.O) {
                int usage = AudioAttributes.USAGE_GAME;
                int contentType = AudioAttributes.CONTENT_TYPE_MUSIC;
                boolean willPauseWhenDucked = true;
                boolean acceptsDelayedFocusGain = true;

                if (attributes != null) {
                    String usageStr = (String) attributes.get("usage");
                    String contentTypeStr = (String) attributes.get("contentType");
                    willPauseWhenDucked = (Boolean) attributes.getOrDefault("willPauseWhenDucked", true);
                    acceptsDelayedFocusGain = (Boolean) attributes.getOrDefault("acceptsDelayedFocusGain", true);

                    usage = getUsageFromString(usageStr);
                    contentType = getContentTypeFromString(contentTypeStr);
                }

                AudioAttributes playbackAttributes = new AudioAttributes.Builder()
                    .setUsage(usage)
                    .setContentType(contentType)
                    .setFlags(AudioAttributes.FLAG_AUDIBILITY_ENFORCED)
                    .build();

                AudioFocusRequest focusRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(playbackAttributes)
                    .setAcceptsDelayedFocusGain(acceptsDelayedFocusGain)
                    .setWillPauseWhenDucked(willPauseWhenDucked)
                    .setOnAudioFocusChangeListener(this)
                    .build();

                int result = audioManager.requestAudioFocus(focusRequest);
                if (result != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                    Log.e("FlutterSoloudPlugin", "Audio focus request failed");
                }
            } else {
                // For older Android versions, we need to specify flags
                int result = audioManager.requestAudioFocus(this,
                        AudioManager.STREAM_MUSIC,
                        AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK);
                
                if (result != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) {
                    Log.e("FlutterSoloudPlugin", "Audio focus request failed");
                }
            }
        }
    }

    private int getUsageFromString(String usage) {
        switch (usage) {
            case "usageAlarm": return AudioAttributes.USAGE_ALARM;
            case "usageAssistanceAccessibility": return AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY;
            case "usageAssistanceNavigationGuidance": return AudioAttributes.USAGE_ASSISTANCE_NAVIGATION_GUIDANCE;
            case "usageAssistanceSonification": return AudioAttributes.USAGE_ASSISTANCE_SONIFICATION;
            case "usageAssistance": return AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY;
            case "usageGame": return AudioAttributes.USAGE_GAME;
            case "usageMedia": return AudioAttributes.USAGE_MEDIA;
            case "usageNotification": return AudioAttributes.USAGE_NOTIFICATION;
            case "usageNotificationCommunicationDelayed": return AudioAttributes.USAGE_NOTIFICATION_COMMUNICATION_DELAYED;
            case "usageNotificationCommunicationInstant": return AudioAttributes.USAGE_NOTIFICATION_COMMUNICATION_INSTANT;
            case "usageNotificationCommunicationRequest": return AudioAttributes.USAGE_NOTIFICATION_COMMUNICATION_REQUEST;
            case "usageNotificationEvent": return AudioAttributes.USAGE_NOTIFICATION_EVENT;
            case "usageNotificationRingtone": return AudioAttributes.USAGE_NOTIFICATION_RINGTONE;
            case "usageVoiceCommunication": return AudioAttributes.USAGE_VOICE_COMMUNICATION;
            case "usageVoiceCommunicationSignalling": return AudioAttributes.USAGE_VOICE_COMMUNICATION_SIGNALLING; // Note the correct spelling
            default: return AudioAttributes.USAGE_GAME;
        }
    }

    private int getContentTypeFromString(String contentType) {
        switch (contentType) {
            case "contentTypeMovie": return AudioAttributes.CONTENT_TYPE_MOVIE;
            case "contentTypeMusic": return AudioAttributes.CONTENT_TYPE_MUSIC;
            case "contentTypeSonification": return AudioAttributes.CONTENT_TYPE_SONIFICATION;
            case "contentTypeSpeech": return AudioAttributes.CONTENT_TYPE_SPEECH;
            default: return AudioAttributes.CONTENT_TYPE_UNKNOWN;
        }
    }

    private void registerHeadsetPlugReceiver() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            MediaRouter mediaRouter = (MediaRouter) context.getSystemService(Context.MEDIA_ROUTER_SERVICE);
            if (mediaRouter == null) {
                Log.e("FlutterSoloudPlugin", "Failed to get MediaRouter service");
                return;
            }

            mediaRouter.addCallback(
                MediaRouter.ROUTE_TYPE_LIVE_AUDIO | 
                MediaRouter.ROUTE_TYPE_USER,
                new MediaRouter.Callback() {
                    @Override
                    public void onRouteAdded(MediaRouter router, RouteInfo info) {
                        updateAudioDeviceStatus();
                    }

                    @Override
                    public void onRouteChanged(MediaRouter router, RouteInfo info) {
                        updateAudioDeviceStatus();
                    }

                    @Override
                    public void onRouteRemoved(MediaRouter router, RouteInfo info) {
                        updateAudioDeviceStatus();
                    }

                    @Override
                    public void onRouteSelected(MediaRouter router, int type, RouteInfo info) {
                        updateAudioDeviceStatus();
                    }

                    @Override
                    public void onRouteUnselected(MediaRouter router, int type, RouteInfo info) {
                        updateAudioDeviceStatus();
                    }

                    @Override
                    public void onRouteVolumeChanged(MediaRouter router, RouteInfo info) {
                    }

                    @Override
                    public void onRouteGrouped(MediaRouter router, RouteInfo info, RouteGroup group, int index) {
                    }

                    @Override
                    public void onRouteUngrouped(MediaRouter router, RouteInfo info, RouteGroup group) {
                    }
                },
                MediaRouter.CALLBACK_FLAG_PERFORM_ACTIVE_SCAN
            );
        }

        // Also keep the broadcast receiver for compatibility and immediate detection
        headsetPlugReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                if (intent.getAction().equals(Intent.ACTION_HEADSET_PLUG)) {
                    updateAudioDeviceStatus();
                }
            }
        };

        IntentFilter filter = new IntentFilter(Intent.ACTION_HEADSET_PLUG);
        // This flag is required for Android 8.0 (API 26) and higher
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.registerReceiver(headsetPlugReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            context.registerReceiver(headsetPlugReceiver, filter);
        }
    }

    private void updateAudioDeviceStatus() {
        if (audioManager != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                AudioDeviceInfo[] devices = audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS);
                boolean headsetConnected = false;
                String deviceName = "";
                boolean hasMic = false;

                for (AudioDeviceInfo device : devices) {
                    if (device.getType() == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
                        device.getType() == AudioDeviceInfo.TYPE_WIRED_HEADPHONES ||
                        device.getType() == AudioDeviceInfo.TYPE_USB_HEADSET ||
                        device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO ||
                        device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
                        headsetConnected = true;
                        deviceName = device.getProductName().toString();
                        hasMic = device.getType() == AudioDeviceInfo.TYPE_WIRED_HEADSET ||
                                device.getType() == AudioDeviceInfo.TYPE_USB_HEADSET ||
                                device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_SCO;
                        break;
                    }
                }
                sendHeadsetInfo(headsetConnected ? 1 : 0, deviceName, hasMic);
            } else {
                // For older versions, we rely on the broadcast receiver info
                boolean isWiredHeadsetOn = audioManager.isWiredHeadsetOn();
                sendHeadsetInfo(isWiredHeadsetOn ? 1 : 0, "Wired Headset", true);
            }
        }
    }

    private void sendHeadsetInfo(int state, String name, boolean hasMicrophone) {
        if (channel != null) {
            java.util.Map<String, Object> headsetInfo = new java.util.HashMap<>();
            headsetInfo.put("state", state);
            headsetInfo.put("name", name != null ? name : "");
            headsetInfo.put("hasMicrophone", hasMicrophone);
            
            channel.invokeMethod("onHeadsetChanged", headsetInfo);
        }
    }

    @Override
    public void onAudioFocusChange(int focusChange) {
        String focusState;
        switch (focusChange) {
            case AudioManager.AUDIOFOCUS_GAIN:
                focusState = "audioFocusGain";
                break;
            case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT:
                focusState = "audioFocusGainTransient";
                break;
            case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE:
                focusState = "audioFocusGainTransientExclusive";
                break;
            case AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK:
                focusState = "audioFocusGainTransientMayDuck";
                break;
            case AudioManager.AUDIOFOCUS_LOSS:
                focusState = "audioFocusLoss";
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
                focusState = "audioFocusLossTransient";
                break;
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                focusState = "audioFocusLossTransientCanDuck";
                break;
            default:
                focusState = "audioFocusNone";
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
        if (headsetPlugReceiver != null) {
            context.unregisterReceiver(headsetPlugReceiver);
            headsetPlugReceiver = null;
        }
        audioManager = null;
        context = null;
        isInitialized = false;
    }
}
