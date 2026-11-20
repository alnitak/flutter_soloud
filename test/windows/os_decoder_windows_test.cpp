// Media Foundation decoders on Windows: NativeAudioDecoder
// (src/native_decoder/os_decoder_windows.cpp: IMFSourceReader, plus the MFT
// path for raw AC-3/E-AC-3) and the streaming AACDecoderWrapper
// (src/audiobuffer/aac_stream_decoder_windows.cpp: the AAC / Dolby MFTs).
//
// Runs the shared contract in test/support/os_decoder_checks.h, then
// Windows-only checks: UTF-8 (non-ASCII) paths, decoding from a thread whose
// COM apartment is already STA, no %TEMP%\sld* files left by decodeMemory's
// fallback, and COM init/uninit staying on the thread that made them when a
// stream decoder is destroyed on another thread.
//
// Built and run by test/windows/os_decoder_windows_test.ps1.
//
//   os_decoder_windows_test.exe <asset dir> <tmp dir>
//
// NOT YET VERIFIED ON WINDOWS: written on macOS and only cross-compiled there
// with mingw-w64. The expectations are the ones every other platform meets
// (priming trimmed, exact lengths); a failure here may be a real Media
// Foundation difference worth looking at, not a broken test.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../support/os_decoder_checks.h"

#include <windows.h>
#include <objbase.h>

#include <string>

using namespace osdec;

namespace {

std::string gDir;
std::string gTmp;

std::wstring widen(const std::string &utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &out[0], n);
    return out;
}

/// Files named sld* in %TEMP%: decodeMemory's GetTempFileName fallback.
long countDecoderTempFiles() {
    wchar_t temp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp) == 0) return -1;
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW((std::wstring(temp) + L"sld*").c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    long n = 0;
    do {
        ++n;
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return n;
}

// decodeFile takes UTF-8; Windows paths are UTF-16. A fixture under a
// non-ASCII directory and file name must still decode.
void testUtf8Path() {
    std::cout << "[Test] decodeFile with a non-ASCII (UTF-8) path" << std::endl;
    const std::string dir = joinPath(gTmp, "d\xC3\xA9j\xC3\xA0 \xE6\x97\xA5\xE6\x9C\xAC");  // "déjà 日本"
    const std::string path = joinPath(dir, "\xC3\xBC" "ber.m4a");                          // "über.m4a"
    CreateDirectoryW(widen(dir).c_str(), nullptr);
    const auto bytes = readFile(joinPath(gDir, "aac_lc_stereo_48000.m4a"));
    FILE *f = _wfopen(widen(path).c_str(), L"wb");
    OSDEC_CHECK(f != nullptr, "cannot create the non-ASCII fixture copy");
    if (!f) return;
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    auto r = decodeFileTimed(path);
    OSDEC_CHECK(r->d.success, "non-ASCII path: " << r->d.errorMessage);
    if (r->d.success) {
        OSDEC_CHECK(r->d.channels == 2 && r->d.sampleRate == 48000.0f, "non-ASCII path: wrong format");
    }
    DeleteFileW(widen(path).c_str());
    RemoveDirectoryW(widen(dir).c_str());
}

// A thread that is already in a single-threaded apartment (a UI thread, or
// an app that called CoInitialize) makes CoInitializeEx(MULTITHREADED) fail
// with RPC_E_CHANGED_MODE; Media Foundation must still work there.
void testStaThread() {
    std::cout << "[Test] Decoding on a thread already initialized as STA" << std::endl;
    bool fileOk = false, memOk = false, streamOk = false;
    std::string error;
    std::thread t([&] {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const std::string path = joinPath(gDir, "aac_lc_mono_44100.m4a");
        DecodedAudioData a = NativeAudioDecoder::decodeFile(path.c_str());
        fileOk = a.success;
        if (!a.success) error = a.errorMessage;
        a.freeSamples();
        const auto bytes = readFile(path);
        DecodedAudioData b = NativeAudioDecoder::decodeMemory(bytes.data(), bytes.size());
        memOk = b.success;
        b.freeSamples();
        AACDecoderWrapper w(DetectedType::BUFFER_AAC);
        streamOk = w.initializeDecoder(0, 0);
        if (SUCCEEDED(hr)) CoUninitialize();
    });
    t.join();
    OSDEC_CHECK(fileOk, "decodeFile on an STA thread: " << error);
    OSDEC_CHECK(memOk, "decodeMemory on an STA thread failed");
    OSDEC_CHECK(streamOk, "AACDecoderWrapper::initializeDecoder on an STA thread failed");
}

// Buffer streams are created and destroyed on different threads, so the
// stream decoder's COM setup must not be undone on (and tear down COM for)
// the destroying thread: it holds the implicit MTA (CoIncrementMTAUsage)
// instead of a per-thread CoInitializeEx.
void testCrossThreadDestroy() {
    std::cout << "[Test] A stream decoder destroyed on another thread leaves that thread's COM alone" << std::endl;
    const HRESULT mainInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    OSDEC_CHECK(mainInit == S_OK || mainInit == S_FALSE, "CoInitializeEx on the main thread: " << mainInit);
    std::unique_ptr<AACDecoderWrapper> wrapper;
    std::thread t([&] {
        wrapper.reset(new AACDecoderWrapper(DetectedType::BUFFER_AAC));
        OSDEC_CHECK(wrapper->initializeDecoder(0, 0), "initializeDecoder on the worker failed");
        auto bytes = readFile(joinPath(gDir, "sample-AAC.aac"));
        std::vector<unsigned char> head(bytes.begin(), bytes.begin() + 16384);
        int rate = 0, channels = 0;
        wrapper->decode(head, &rate, &channels);
    });
    t.join();
    wrapper.reset();  // destroyed on the main thread
    // If the destructor's CoUninitialize ran here, the main thread's own
    // initialization above is gone and this re-initializes (S_OK) instead of
    // nesting (S_FALSE).
    const HRESULT again = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    OSDEC_CHECK(again == S_FALSE, "the main thread's COM was uninitialized by a decoder made on another thread"
                                      " (CoInitializeEx returned " << again << ", expected S_FALSE)");
    if (SUCCEEDED(again)) CoUninitialize();
    if (SUCCEEDED(mainInit)) CoUninitialize();
}

} // namespace

int main(int argc, char **argv) {
    gDir = argc > 1 ? argv[1] : "example\\assets\\audio";
    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    gTmp = argc > 2 && argv[2][0] ? argv[2] : temp;

    std::string missing;
    if (!haveFixtures(gDir, missing)) {
        std::cout << "SKIPPED: fixture not found: " << missing << std::endl;
        return 0;
    }

    Policy policy;
    policy.memFileTolerance = 1e-6f;
    // Unknown for Media Foundation; allowed as on Apple and FFmpeg.
    policy.truncatedM4aMayDecode = true;
    // Windows N editions ship without the Dolby decoder MFT.
    policy.ac3Optional = true;

    const long tempBefore = countDecoderTempFiles();
    runAll(policy, gDir, gTmp, "");
    OSDEC_CHECK(countDecoderTempFiles() == tempBefore, "decodeMemory left %TEMP%\\sld* files behind");
    testUtf8Path();
    testStaThread();
    testCrossThreadDestroy();
    return finish("WINDOWS OS DECODER");
}
