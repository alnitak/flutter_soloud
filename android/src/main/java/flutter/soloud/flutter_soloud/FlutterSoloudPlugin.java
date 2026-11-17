package flutter.soloud.flutter_soloud;

import android.util.Log;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import io.flutter.embedding.engine.FlutterEngine;
import io.flutter.embedding.engine.plugins.FlutterPlugin;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;

/**
 * Keeps flutter_soloud's process-global native state in step with the lifetime
 * of the FlutterEngine that owns it.
 *
 * <p>The native engine (the SoLoud player, its output device, its lifecycle
 * scheduler and the registered Dart callback pointers) lives for the whole
 * process, while the Dart isolate that drives it belongs to a single
 * FlutterEngine. When an engine goes away but the process keeps running --
 * routine for a foreground-service audio app, e.g. audio_service -- native code
 * would otherwise keep calling into NativeCallables whose isolate is gone
 * (undefined behaviour) and keep an output device running with nothing left to
 * control it. Only the embedder can observe that transition: Dart's
 * {@code detached} lifecycle state is not guaranteed to arrive first, and there
 * is no reliable root-isolate exit hook.
 *
 * <p>This tracks the FlutterEngine, not the Activity. A cached engine
 * deliberately outlives Activity recreation, and tearing the audio engine down
 * because the user rotated the screen would be a bug.
 */
public final class FlutterSoloudPlugin implements FlutterPlugin {
    private static final String TAG = "FlutterSoloudPlugin";

    /**
     * The native library is loaded lazily, at the first point one of the
     * hooks below actually needs to call into it -- never from a static
     * initializer and never from {@link #onAttachedToEngine}. Both of those run
     * at engine startup (GeneratedPluginRegistrant instantiates and attaches
     * every plugin), which would drag the whole multi-megabyte library onto the
     * main thread during app launch even for an app that never plays a sound.
     *
     * <p>Nor is it loaded by a hook unless something in the process already
     * has. An app that uses SoLoud has loaded it from Dart (its first
     * {@code @Native} call) by the time a hook fires, so
     * {@code System.loadLibrary} is a refcount bump and the hook calls into it
     * synchronously, as it must. An app that never touched SoLoud in this
     * process has not, and then there is nothing for a hook to do -- see
     * {@link NativeLibraryGate} -- so it skips the load instead of doing a real
     * one on the main thread mid-teardown.
     *
     * <p>Two signals say the library is in use, and a hook is skipped only
     * when neither does. The first is Dart's: before it creates any state a
     * teardown has to undo, a Dart isolate sends
     * {@value #METHOD_NATIVE_LIBRARY_IN_USE} on
     * {@value #ENGINE_LIFECYCLE_CHANNEL}, and from then on no hook in this
     * process skips. The second is the marker the library leaves in
     * /proc/self/maps ({@link ProcessMapsProbe}), for an isolate that could not
     * send the first. The marker alone is not enough: it can fail to publish
     * while the library loads regardless, and an unmarked library looks
     * unloaded.
     *
     * <p>Successful loads are cached; a skipped or failed load is retried by
     * the next hook.
     */
    private static final NativeLibraryGate NATIVE_LIBRARY =
        new NativeLibraryGate(
            new ProcessMapsProbe(),
            () -> System.loadLibrary("flutter_soloud_plugin"));

    private static native boolean
        nativeClearDartCallbackRegistrationsForEngine(long engineId);

    private static native boolean
        nativeRequestEngineTeardownForEngine(long engineId);

    /**
     * The engine-local channel Dart reaches this plugin on. iOS and macOS use
     * the same name for their own engine-lifecycle handshake.
     */
    static final String ENGINE_LIFECYCLE_CHANNEL =
        "flutter_soloud/engine_lifecycle";

    /**
     * Sent by a Dart isolate before it creates native state that a teardown
     * has to undo. Must match AndroidEngineLifecycle in
     * lib/src/bindings/android_engine_lifecycle.dart.
     */
    static final String METHOD_NATIVE_LIBRARY_IN_USE = "nativeLibraryInUse";

    @Nullable private FlutterEngine flutterEngine;
    @Nullable private Long engineId;
    @Nullable private FlutterEngine.EngineLifecycleListener lifecycleListener;
    @Nullable private MethodChannel lifecycleChannel;

    /**
     * onEngineWillDestroy() and onDetachedFromEngine() both fire on a real
     * engine destroy; the teardown must only be requested once.
     */
    private boolean teardownRequested = false;

    // getFlutterEngine() is deprecated in favour of the binary messenger /
    // texture registry / platform view registry accessors, but this plugin
    // wants none of those: it needs the engine itself, for its id and for its
    // lifecycle listener, and no other accessor exposes them.
    @SuppressWarnings("deprecation")
    @Override
    public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
        // Deliberately does no native work. This runs during app launch for
        // every app that depends on the plugin, whether or not it ever uses
        // SoLoud, so it must stay pure Java bookkeeping: read the engine id,
        // register a listener and a channel handler. Nothing here loads the
        // native library, opens a device, or starts a thread.
        final FlutterEngine engine = binding.getFlutterEngine();
        flutterEngine = engine;
        engineId = engine.getEngineId();
        teardownRequested = false;

        // Ensure TMPDIR points to the app cache dir so native decoders
        // and encoders (AMediaExtractor, libflac) have a writable scratch directory.
        try {
            final java.io.File cacheDir = binding.getApplicationContext().getCacheDir();
            if (cacheDir != null) {
                android.system.Os.setenv("TMPDIR", cacheDir.getAbsolutePath(), false);
            }
        } catch (Throwable ignored) {
        }

        final FlutterEngine.EngineLifecycleListener listener =
            new FlutterEngine.EngineLifecycleListener() {
                @Override
                public void onPreEngineRestart() {
                    // Hot restart replaces the Dart isolate but does not detach
                    // plugins, and the engine id is unchanged -- so without this
                    // the registered NativeCallables silently go stale. Only the
                    // bridges are cleared: the engine itself is still owned by
                    // this FlutterEngine, and the new isolate's init() finds the
                    // native engine initialized and deinits it itself.
                    clearDartCallbackRegistrations();
                }

                @Override
                public void onEngineWillDestroy() {
                    // Fires just before the plugin registry is destroyed. The
                    // engine is still valid here, so this is the earliest safe
                    // point.
                    requestEngineTeardown();
                }
            };
        lifecycleListener = listener;
        engine.addEngineLifecycleListener(listener);

        final MethodChannel channel = new MethodChannel(
            binding.getBinaryMessenger(), ENGINE_LIFECYCLE_CHANNEL);
        channel.setMethodCallHandler(FlutterSoloudPlugin::onEngineLifecycleCall);
        lifecycleChannel = channel;
    }

    /**
     * Handles the engine-local lifecycle channel, on the platform thread.
     *
     * <p>{@value #METHOD_NATIVE_LIBRARY_IN_USE} only flips a flag in
     * {@link NativeLibraryGate}: it loads nothing. Dart sends it after its own
     * first native call has loaded the library and before it takes the engine
     * claim or registers callbacks, so by the time a hook acts on it the load
     * there is a refcount bump.
     */
    private static void onEngineLifecycleCall(
        @NonNull MethodCall call, @NonNull MethodChannel.Result result) {
        if (METHOD_NATIVE_LIBRARY_IN_USE.equals(call.method)) {
            NATIVE_LIBRARY.markInUse();
            result.success(true);
        } else {
            result.notImplemented();
        }
    }

    @Override
    public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
        final FlutterEngine engine = flutterEngine;
        final FlutterEngine.EngineLifecycleListener listener = lifecycleListener;
        final MethodChannel channel = lifecycleChannel;

        if (engine != null && listener != null) {
            engine.removeEngineLifecycleListener(listener);
        }
        if (channel != null) {
            channel.setMethodCallHandler(null);
        }

        // Requested here too: onEngineWillDestroy() is not reached on every
        // detach path, and requestEngineTeardown() is idempotent.
        requestEngineTeardown();

        flutterEngine = null;
        engineId = null;
        lifecycleListener = null;
        lifecycleChannel = null;
    }

    private void clearDartCallbackRegistrations() {
        final Long id = engineId;
        // Skipped only when Dart has never said it uses the library and the
        // library is not marked as loaded. Then no isolate in this process has
        // registered a callback, so there is nothing to clear.
        if (id == null || !NATIVE_LIBRARY.loadIfAlreadyInProcess()) {
            return;
        }

        try {
            nativeClearDartCallbackRegistrationsForEngine(id);
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "Unable to clear Dart callback registrations", error);
        }
    }

    /**
     * Drops the Dart bridges and asks native code to tear the engine down. The
     * blocking part of the teardown (stopping the device, joining the lifecycle
     * scheduler) runs on a native worker thread, so this returns promptly and
     * never blocks the platform thread.
     */
    private void requestEngineTeardown() {
        final Long id = engineId;
        if (id == null || teardownRequested) {
            return;
        }

        // Marked as requested only once native code has accepted it. Setting it
        // up front would make a failed library load, a failed JNI call, or a
        // native worker that could not be spawned terminal: onDetachedFromEngine()
        // is the retry for a hook that ran too early or hit a load that can still
        // succeed later, and it would have found the flag already set.
        //
        // A native `false` is not always retryable -- it also means another
        // engine owns the native engine, or nothing is claimed -- but retrying
        // those costs one rejected call and keeps the recoverable cases working.
        //
        // A library that is not loaded yet is the same "nothing is claimed"
        // case, answered without loading it: the claim and the callbacks are
        // only ever created by Dart calling into the library, and Dart or the
        // library's marker says so before they exist. It is left retryable
        // too, since Dart can still load it before the next hook.
        if (!NATIVE_LIBRARY.loadIfAlreadyInProcess()) {
            return;
        }

        try {
            teardownRequested = nativeRequestEngineTeardownForEngine(id);
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "Unable to request native engine teardown", error);
        }
    }
}
