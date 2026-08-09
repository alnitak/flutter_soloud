package flutter.soloud.flutter_soloud;

import android.util.Log;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import io.flutter.embedding.engine.FlutterEngine;
import io.flutter.embedding.engine.plugins.FlutterPlugin;

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
     * Guarded by the class monitor.
     *
     * <p>The native library is loaded lazily, at the first point one of the
     * hooks below actually needs to call into it -- never from a static
     * initializer and never from {@link #onAttachedToEngine}. Both of those run
     * at engine startup (GeneratedPluginRegistrant instantiates and attaches
     * every plugin), which would drag the whole multi-megabyte library onto the
     * main thread during app launch even for an app that never plays a sound.
     *
     * <p>By the time a hook fires, an app that uses SoLoud has already loaded
     * the same library from Dart via {@code DynamicLibrary.open}, so
     * {@code System.loadLibrary} is a refcount bump rather than a real load.
     * Successful loads are cached; failed loads can be retried by a later hook.
     */
    private static boolean nativeLibraryLoaded = false;

    private static native boolean
        nativeClearDartCallbackRegistrationsForEngine(long engineId);

    private static native boolean
        nativeRequestEngineTeardownForEngine(long engineId);

    @Nullable private FlutterEngine flutterEngine;
    @Nullable private Long engineId;
    @Nullable private FlutterEngine.EngineLifecycleListener lifecycleListener;

    /**
     * onEngineWillDestroy() and onDetachedFromEngine() both fire on a real
     * engine destroy; the teardown must only be requested once.
     */
    private boolean teardownRequested = false;

    private static synchronized boolean ensureNativeLibraryLoaded() {
        if (nativeLibraryLoaded) {
            return true;
        }

        try {
            System.loadLibrary("flutter_soloud_plugin");
            nativeLibraryLoaded = true;
            return true;
        } catch (UnsatisfiedLinkError error) {
            // Never fail engine teardown over this. The FFI layer opens the
            // same library from Dart and surfaces the error there, where it is
            // catchable and actionable.
            return false;
        }
    }

    // getFlutterEngine() is deprecated in favour of the binary messenger /
    // texture registry / platform view registry accessors, but this plugin
    // wants none of those: it needs the engine itself, for its id and for its
    // lifecycle listener, and no other accessor exposes them.
    @SuppressWarnings("deprecation")
    @Override
    public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
        // Deliberately does no native work. This runs during app launch for
        // every app that depends on the plugin, whether or not it ever uses
        // SoLoud, so it must stay pure Java bookkeeping: read the engine id and
        // register a listener. Nothing here loads the native library, opens a
        // device, or starts a thread.
        final FlutterEngine engine = binding.getFlutterEngine();
        flutterEngine = engine;
        engineId = engine.getEngineId();
        teardownRequested = false;

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
    }

    @Override
    public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
        final FlutterEngine engine = flutterEngine;
        final FlutterEngine.EngineLifecycleListener listener = lifecycleListener;

        if (engine != null && listener != null) {
            engine.removeEngineLifecycleListener(listener);
        }

        // Requested here too: onEngineWillDestroy() is not reached on every
        // detach path, and requestEngineTeardown() is idempotent.
        requestEngineTeardown();

        flutterEngine = null;
        engineId = null;
        lifecycleListener = null;
    }

    private void clearDartCallbackRegistrations() {
        final Long id = engineId;
        if (id == null || !ensureNativeLibraryLoaded()) {
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
        if (!ensureNativeLibraryLoaded()) {
            return;
        }

        try {
            teardownRequested = nativeRequestEngineTeardownForEngine(id);
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "Unable to request native engine teardown", error);
        }
    }
}
