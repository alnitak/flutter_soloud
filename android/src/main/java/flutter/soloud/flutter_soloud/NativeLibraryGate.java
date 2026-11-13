package flutter.soloud.flutter_soloud;

/**
 * Decides whether an engine lifecycle hook may call into the native library,
 * and loads it from Java only when doing so is a refcount bump.
 *
 * <p>The hooks that need the library run on the platform thread during engine
 * teardown and hot restart. There are only two cases:
 *
 * <ul>
 *   <li>Something in the process has already loaded the library (Dart's first
 *       {@code @Native} call into SoLoud does). Loading it again from Java
 *       only bumps the linker's refcount, and the hook must then make its
 *       native call synchronously, exactly as before.
 *   <li>Nothing has. Then no Dart code in this process has ever called into
 *       SoLoud, so the native engine holds no lifecycle claim and no Dart
 *       callback registration, and both JNI entry points are guaranteed no-ops
 *       that return false. Loading the library would be a real multi-megabyte
 *       load on the platform thread only to learn that, so it is skipped.
 * </ul>
 *
 * <p>Two signals tell the cases apart, and only the second can ever skip a
 * hook:
 *
 * <ul>
 *   <li>{@link #markInUse}: Dart says so. Before it creates any state a
 *       teardown has to undo -- the engine claim, the callback registration --
 *       a Dart isolate on Android tells the plugin over the engine-local
 *       {@code flutter_soloud/engine_lifecycle} channel, after its own first
 *       native call has loaded the library. From then on every hook loads and
 *       calls in without asking the {@link Probe}. The signal is sticky and
 *       process-wide: the native state outlives the isolate and the
 *       FlutterEngine that created it, so nothing short of the process ending
 *       makes skipping safe again.
 *   <li>The {@link Probe}: the library says so, with a marker it leaves in
 *       /proc/self/maps when it is loaded. It covers an isolate that could not
 *       send the signal: one with no Flutter messaging, or one whose engine
 *       does not have the plugin registered.
 * </ul>
 *
 * <p>The marker alone is not enough because publishing it can fail. It takes
 * an {@code mmap} and a {@code prctl}, and either can fail (out of memory, a
 * process at {@code vm.max_map_count}) while the library loads all the same:
 * the library cannot refuse to load over its marker. An unmarked library
 * looks exactly like an absent one, and a hook skipped on that answer leaves
 * the engine claim and the Dart callbacks of a destroyed FlutterEngine behind.
 * Native code retries the marker at the points where Dart creates that state,
 * but a retry can fail too. The Dart signal depends on none of that.
 *
 * <p>Skipping reports "not loaded" without caching it: Dart can still load the
 * library later (before the next hook), and the next hook asks again.
 * Loading successfully is cached for good: ART keeps every library it loads
 * for the lifetime of the class loader.
 *
 * <p>Pure Java, free of Android and Flutter types, so the decision can be unit
 * tested on the JVM with a fake {@link Probe} and {@link Loader}.
 */
final class NativeLibraryGate {
    /** Answers "is the library already in this process?" without loading it. */
    interface Probe {
        /**
         * True when the library is already loaded in this process, or when
         * that cannot be ruled out. Must be cheap enough for the platform
         * thread and must never load the library itself.
         *
         * <p>A false answer is what allows a hook to be skipped, so it must
         * mean "definitely not loaded". Only asked until Dart has called
         * {@link #markInUse}.
         */
        boolean mayBeLoaded();
    }

    /** Loads the library, e.g. {@code System.loadLibrary}. */
    interface Loader {
        /** @throws UnsatisfiedLinkError when the library cannot be loaded. */
        void load();
    }

    private final Probe probe;
    private final Loader loader;

    /** Guarded by this. */
    private boolean loaded = false;

    /** Set by {@link #markInUse}, never cleared. Guarded by this. */
    private boolean inUse = false;

    NativeLibraryGate(Probe probe, Loader loader) {
        this.probe = probe;
        this.loader = loader;
    }

    /**
     * Records that Dart in this process uses the library, so that from now on
     * no hook skips its native call, whatever the {@link Probe} says.
     *
     * <p>Dart sends this before it creates any state a teardown has to undo,
     * and after its own first native call has loaded the library, so a later
     * {@link #loadIfAlreadyInProcess} is still only a refcount bump.
     *
     * <p>Pure bookkeeping: it loads nothing, so it is safe on the platform
     * thread at any time. Idempotent, and never undone.
     */
    synchronized void markInUse() {
        inUse = true;
    }

    /**
     * Returns true when the library is loaded and its JNI entry points may be
     * called. Loads it from Java only when Dart has said it uses the library
     * ({@link #markInUse}) or the probe says something in the process already
     * has it, so this never performs a real load.
     *
     * <p>Returns false, without loading anything, when Dart has not said so
     * and the library is definitely not in the process -- the caller then has
     * nothing to do -- or when the load fails. Neither is cached, so a later
     * hook tries again. Never throws.
     */
    synchronized boolean loadIfAlreadyInProcess() {
        if (loaded) {
            return true;
        }

        boolean mayBeLoaded;
        if (inUse) {
            // Dart has created native state, or is about to. All the probe
            // could add is a wrong "absent" from a marker that failed to
            // publish, so it is not asked.
            mayBeLoaded = true;
        } else {
            try {
                mayBeLoaded = probe.mayBeLoaded();
            } catch (Throwable error) {
                // A probe that cannot answer must not turn into "not loaded":
                // that is the answer that skips a teardown. Fall back to
                // loading, which is what the plugin always did before it had
                // a probe.
                mayBeLoaded = true;
            }
        }

        if (!mayBeLoaded) {
            return false;
        }

        try {
            loader.load();
            loaded = true;
            return true;
        } catch (UnsatisfiedLinkError error) {
            // Never fail engine teardown over this. The FFI layer opens the
            // same library from Dart and surfaces the error there, where it is
            // catchable and actionable.
            return false;
        }
    }
}
