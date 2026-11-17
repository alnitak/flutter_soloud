package flutter.soloud.flutter_soloud;

import static org.junit.Assert.assertEquals;

import flutter.soloud.flutter_soloud.ProcessMapsProbe.Result;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import org.junit.Test;

public class ProcessMapsProbeTest {
    // Shaped like a real Android process's /proc/self/maps. With the default
    // packaging the library itself is mapped from base.apk and never appears
    // under its own name, which is why the probe looks for the marker instead.
    private static final String NAMED_ANON =
        "12c00000-2ac00000 rw-p 00000000 00:00 0"
            + "                                  [anon:dalvik-main space (region space)]\n";
    private static final String APK =
        "7a3c20a4c000-7a3c20d1f000 r-xp 00a4c000 fd:05 1234"
            + "                       /data/app/~~ab==/com.example.app-cd==/base.apk\n";
    private static final String UNNAMED_ANON =
        "7a3c21000000-7a3c21200000 rw-p 00000000 00:00 0 \n";
    private static final String STACK =
        "7ffd4a1e0000-7ffd4a201000 rw-p 00000000 00:00 0"
            + "                                  [stack]\n";
    private static final String MARKER =
        "7a3c22c3f000-7a3c22c40000 ---p 00000000 00:00 0"
            + "                                  [anon:flutter_soloud_plugin]\n";

    private static Result scan(String maps) throws IOException {
        return ProcessMapsProbe.scan(trickle(maps, Integer.MAX_VALUE));
    }

    /** Returns [text] at most [maxChunk] bytes per read, like a pipe would. */
    private static InputStream trickle(String text, int maxChunk) {
        final byte[] bytes = text.getBytes(StandardCharsets.US_ASCII);
        return new InputStream() {
            private int position = 0;

            @Override
            public int read() {
                return position < bytes.length ? bytes[position++] & 0xff : -1;
            }

            @Override
            public int read(byte[] buffer, int offset, int length) {
                if (length == 0) {
                    return 0;
                }
                if (position >= bytes.length) {
                    return -1;
                }
                final int count =
                    Math.min(Math.min(length, maxChunk), bytes.length - position);
                System.arraycopy(bytes, position, buffer, offset, count);
                position += count;
                return count;
            }
        };
    }

    private static String repeat(String text, int times) {
        final StringBuilder builder = new StringBuilder();
        for (int i = 0; i < times; i++) {
            builder.append(text);
        }
        return builder.toString();
    }

    @Test
    public void markerIsPresent() throws IOException {
        assertEquals(Result.PRESENT, scan(NAMED_ANON + APK + MARKER + STACK));
    }

    @Test
    public void namedAnonymousMappingsWithoutTheMarkerAreAbsent() throws IOException {
        assertEquals(Result.ABSENT, scan(NAMED_ANON + APK + UNNAMED_ANON + STACK));
    }

    @Test
    public void noNamedAnonymousMappingAtAllIsUnknown() throws IOException {
        // A kernel that cannot name mappings cannot show the marker either, so
        // its absence must not read as "not loaded".
        assertEquals(Result.UNKNOWN, scan(APK + UNNAMED_ANON + STACK));
        assertEquals(Result.UNKNOWN, scan(""));
    }

    @Test
    public void lookAlikeNamesAreNotTheMarker() throws IOException {
        final String longerName =
            "7a3c22c3f000-7a3c22c40000 ---p 00000000 00:00 0"
                + "      [anon:flutter_soloud_plugin_scratch]\n";
        final String prefixedName =
            "7a3c22c3f000-7a3c22c40000 ---p 00000000 00:00 0"
                + "      [anon:not_flutter_soloud_plugin]\n";
        final String inAPath =
            "7a3c22c3f000-7a3c22c40000 r--p 00000000 fd:05 99"
                + "      /data/[anon:flutter_soloud_plugin]/x.so\n";

        assertEquals(
            Result.ABSENT, scan(NAMED_ANON + longerName + prefixedName + inAPath));
    }

    @Test
    public void markerSplitAcrossReadsIsFound() throws IOException {
        final String maps = repeat(NAMED_ANON + APK, 50) + MARKER + STACK;
        for (int chunk = 1; chunk <= 64; chunk++) {
            assertEquals(
                "chunk " + chunk, Result.PRESENT, ProcessMapsProbe.scan(trickle(maps, chunk)));
        }
    }

    @Test
    public void namedAnonymousMappingSplitAcrossReadsIsFound() throws IOException {
        final String maps = APK + NAMED_ANON + STACK;
        for (int chunk = 1; chunk <= 16; chunk++) {
            assertEquals(
                "chunk " + chunk, Result.ABSENT, ProcessMapsProbe.scan(trickle(maps, chunk)));
        }
    }

    @Test
    public void largeProcessIsScannedToTheEnd() throws IOException {
        // Several buffers' worth of mappings, with the marker last.
        final String maps = repeat(NAMED_ANON + APK + UNNAMED_ANON + STACK, 2000);

        assertEquals(Result.ABSENT, scan(maps));
        assertEquals(Result.PRESENT, scan(maps + MARKER));
    }

    @Test
    public void linesLongerThanOneReadDoNotHideTheMarker() throws IOException {
        final String longPath =
            "7a3c20a4c000-7a3c20d1f000 r--p 00000000 fd:05 1234 /"
                + repeat("d", 200 * 1024) + "\n";

        assertEquals(Result.PRESENT, scan(longPath + MARKER));
        assertEquals(Result.PRESENT, scan(NAMED_ANON + longPath + MARKER + longPath));
        assertEquals(Result.ABSENT, scan(longPath + NAMED_ANON + longPath));
    }

    @Test
    public void lastLineWithoutNewlineIsStillRead() throws IOException {
        final String unterminated = MARKER.substring(0, MARKER.length() - 1);

        assertEquals(Result.PRESENT, scan(NAMED_ANON + unterminated));
    }
}
