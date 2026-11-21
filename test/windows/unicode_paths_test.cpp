// Non-ASCII, space-containing and long (> MAX_PATH) paths on Windows,
// through the plugin's own entry points (bindings.cpp, linked whole):
// loadFile() in memory and disk mode for WAV, MP3, FLAC (SoLoud's DiskFile
// path) and M4A (Media Foundation, src/native_decoder/os_decoder_windows.cpp,
// and the native stream for disk mode), and loadMem() of an M4A while %TMP%
// is a non-ASCII directory (decodeMemory's temporary-file fallback).
//
// Paths are UTF-8, as Dart hands them over. Each fixture is copied from
// example\assets\audio into the test directory with wide-character APIs.
//
// Long paths: a path over 260 characters is tried as is (works only when the
// process is long-path aware and LongPathsEnabled is set; reported, not
// required) and with the \\?\ prefix, which the wide APIs accept everywhere.
//
//   unicode_paths_test.exe <asset dir> <work dir> [--strict]

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "bindings.h"
#include "enums.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static int gChecks = 0, gFailures = 0, gKnownBugs = 0;
static bool gStrict = false;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

#define KNOWN_BUG(id, cond, ...)                                               \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            if (gStrict) ++gFailures; else ++gKnownBugs;                       \
            std::printf("  %s %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",\
                        id, __FILE__, __LINE__, #cond);                        \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

static std::wstring widen(const std::string &s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static std::wstring extended(const std::wstring &p)
{
    return p.rfind(L"\\\\?\\", 0) == 0 ? p : L"\\\\?\\" + p;
}

static bool makeDirs(const std::string &utf8)
{
    // Component by component, each with the \\?\ prefix so a long path
    // works too.
    const std::wstring w = widen(utf8);
    for (size_t i = 3; i <= w.size(); i++)
    {
        if (i == w.size() || w[i] == L'\\')
        {
            const std::wstring part = extended(w.substr(0, i));
            if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            {
                std::printf("  CreateDirectoryW(%ls) failed: %lu\n", part.c_str(), GetLastError());
                return false;
            }
        }
    }
    const DWORD a = GetFileAttributesW(extended(w).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool copyFixture(const std::string &from, const std::string &toUtf8)
{
    return CopyFileW(widen(from).c_str(), extended(widen(toUtf8)).c_str(), FALSE) != 0;
}

// loadFile() reports through the file-loaded callback, synchronously.
static std::mutex gMutex;
static PlayerErrors gLoadError = noError;
static unsigned gLoadHash = 0;
static bool gLoadDone = false;
static void onVoiceEnded(unsigned int *h) { std::free(h); }
static void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h, uint64_t *c)
{
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gLoadDone = true;
        gLoadError = e ? *e : unknownError;
        gLoadHash = h ? *h : 0;
    }
    std::free(e);
    std::free(n);
    std::free(h);
    std::free(c);
}
static void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

static PlayerErrors load(const std::string &utf8, bool intoMem, unsigned &hash)
{
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gLoadDone = false;
    }
    std::string p = utf8;
    loadFile(&p[0], intoMem, 0);
    std::lock_guard<std::mutex> lock(gMutex);
    hash = gLoadDone ? gLoadHash : 0;
    return gLoadDone ? gLoadError : unknownError;
}

// Loads [path] both ways; returns true when both worked and the sound has
// a plausible length.
static bool loadBothWays(const std::string &path, const char *label, bool required, const char *bugId = nullptr)
{
    bool all = true;
    for (bool intoMem : {true, false})
    {
        unsigned hash = 0;
        const PlayerErrors e = load(path, intoMem, hash);
        const double len = hash ? getLength(hash) : 0;
        std::printf("  %-34s %-6s -> %d, %.2f s\n", label, intoMem ? "memory" : "disk", e, len);
        const bool ok = (e == noError || e == fileAlreadyLoaded) && hash != 0 && len > 0.2;
        if (required && bugId)
            KNOWN_BUG(bugId, ok, "%s (%s): error %d", label, intoMem ? "memory" : "disk", e);
        else if (required)
            CHECK(ok, "%s (%s): error %d, length %.2f", label, intoMem ? "memory" : "disk", e, len);
        all = all && ok;
        if (hash) disposeSound(hash);
    }
    return all;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("usage: unicode_paths_test <asset dir> <work dir> [--strict]\n");
        return 2;
    }
    const std::string assets = argv[1];
    const std::string work = argv[2];
    for (int i = 3; i < argc; i++)
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;

    prepareEngineInit(1);
    if (initEngine(-1, 44100, 2048, 2, 0, 0, 0) != noError)
    {
        std::printf("SKIPPED: the engine could not start (no audio device)\n");
        return 0;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 1);

    struct Fixture
    {
        const char *src;
        const char *ext;
    };
    const Fixture fixtures[] = {{"sample-WAV.wav", "wav"},
                                {"sample-MP3.mp3", "mp3"},
                                {"sample-FLAC.flac", "flac"},
                                {"aac_lc_stereo_48000.m4a", "m4a"}};

    // "déjà vu 日本語 🎵" and friends; UTF-8 written out so the source file's
    // encoding cannot matter.
    const std::string uni = "d\xC3\xA9j\xC3\xA0 vu \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x8E\xB5";
    const std::string fileUni = "\xC3\xBC" "ber s\xC3\xB6ng \xE2\x99\xAA";

    std::printf("non-ASCII and spaces\n");
    const std::string dir = work + "\\" + uni + "\\sub dir";
    CHECK(makeDirs(dir), "create %s", dir.c_str());
    for (const auto &f : fixtures)
    {
        const std::string to = dir + "\\" + fileUni + "." + f.ext;
        if (!copyFixture(assets + "\\" + f.src, to))
        {
            CHECK(false, "copy %s", f.src);
            continue;
        }
        loadBothWays(to, (std::string("unicode .") + f.ext).c_str(), true);
    }

    std::printf("long paths (> MAX_PATH)\n");
    std::string longDir = work;
    int level = 0;
    while (longDir.size() < 300)
        longDir += "\\" + std::string("long directory name number ") + std::to_string(level++) + " \xC3\xA9";
    CHECK(makeDirs(longDir), "create the long directory (%zu bytes)", longDir.size());
    for (const auto &f : fixtures)
    {
        const std::string to = longDir + "\\" + fileUni + "." + f.ext;
        if (!copyFixture(assets + "\\" + f.src, to))
        {
            CHECK(false, "copy %s to the long path", f.src);
            continue;
        }
        // As is: depends on the process being long-path aware.
        loadBothWays(to, (std::string("long, plain .") + f.ext).c_str(), false);
        // With \\?\: the wide APIs take it on every Windows version.
        loadBothWays("\\\\?\\" + to, (std::string("long, \\\\?\\ .") + f.ext).c_str(), true);
    }

    std::printf("decodeMemory temporary file under a non-ASCII %%TMP%%\n");
    const std::string tmp = work + "\\" + uni + "\\tmp \xC3\xA5";
    CHECK(makeDirs(tmp), "create %s", tmp.c_str());
    wchar_t oldTmp[MAX_PATH] = L"";
    GetEnvironmentVariableW(L"TMP", oldTmp, MAX_PATH);
    SetEnvironmentVariableW(L"TMP", widen(tmp).c_str());
    SetEnvironmentVariableW(L"TEMP", widen(tmp).c_str());
    {
        FILE *f = _wfopen(widen(assets + "\\aac_lc_stereo_48000.m4a").c_str(), L"rb");
        std::vector<unsigned char> bytes;
        if (f)
        {
            unsigned char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            fclose(f);
        }
        for (int intoMem : {1, 0})
        {
            unsigned hash = 0;
            std::string name = "mem_" + uni + (intoMem ? "_a.m4a" : "_b.m4a");
            const PlayerErrors e = loadMem(&name[0], bytes.data(), (int)bytes.size(), intoMem, &hash);
            std::printf("  loadMem(m4a, %s) with TMP=%s -> %d\n", intoMem ? "memory" : "disk", tmp.c_str(), e);
            CHECK(e == noError && hash != 0 && getLength(hash) > 1.5,
                  "loadMem of an M4A with a non-ASCII TMP: error %d", e);
            if (hash) disposeSound(hash);
        }
        // Nothing left behind in the temporary directory.
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((widen(tmp) + L"\\*").c_str(), &fd);
        int leftovers = 0;
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) leftovers++;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        CHECK(leftovers == 0, "%d temporary file(s) left in TMP", leftovers);
    }
    SetEnvironmentVariableW(L"TMP", oldTmp[0] ? oldTmp : nullptr);

    dispose();
    std::printf("%d checks, %d failed, %d known bugs\n", gChecks, gFailures, gKnownBugs);
    return gFailures == 0 ? 0 : 1;
}
