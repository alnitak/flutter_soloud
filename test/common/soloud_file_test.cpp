// Unit tests for SoLoud's file classes (src/soloud/src/core/soloud_file.cpp),
// which carry flutter_soloud local patches: DiskFile without an open file
// acts as an empty file, MemoryFile::seek clamps.
//
// Verifies:
//   disk         DiskFile: a missing file, a 0-byte file, reads past the end,
//                seeks past the end and to negative offsets, a file deleted
//                or truncated while it is open, reopening, the FILE*
//                constructor
//   memory       MemoryFile: openMem with no data, reads past the end and
//                with a byte count that wraps the offset, negative seeks
//                (from the end) and seeks past the end, a 0-byte length,
//                read8/16/32 at the end
//   to-mem       MemoryFile::openToMem/openFileToMem: a 0-byte file, a
//                missing file after an earlier successful open, a source
//                that reads fewer bytes than its length (a file truncated
//                between length() and read())
//   filehack     the stdio replacements: fopen of a missing file, fseek with
//                SEEK_SET/SEEK_CUR/SEEK_END, ftell, fread, fgetc at the end
//
// Needs no audio device and no assets (files made in a temp directory).
//
//   ./test/common/soloud_file_test.sh [--asan] [--strict]
//
// --strict makes the checks marked KNOWN BUG fail the run.

#include "soloud.h"
#include "soloud_file.h"
#include "../support/temp_dir.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" {
int Soloud_Filehack_fgetc(Soloud_Filehack *f);
int Soloud_Filehack_fread(void *dst, int s, int c, Soloud_Filehack *f);
int Soloud_Filehack_fseek(Soloud_Filehack *f, int idx, int base);
int Soloud_Filehack_ftell(Soloud_Filehack *f);
int Soloud_Filehack_fclose(Soloud_Filehack *f);
Soloud_Filehack *Soloud_Filehack_fopen(const char *aFilename, char *aMode);
}

using SoLoud::DiskFile;
using SoLoud::MemoryFile;

static int gFailures = 0;
static int gChecks = 0;
static int gKnownBugs = 0;
static bool gStrict = false;

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      ++gFailures;                                                             \
      std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);           \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

// A check that fails because of a known bug in src/ (see where it is used).
// Reported, but fails the run only with --strict.
#define KNOWN_BUG(cond, ...)                                                   \
  do {                                                                         \
    ++gChecks;                                                                 \
    if (!(cond)) {                                                             \
      if (gStrict) ++gFailures; else ++gKnownBugs;                             \
      std::printf("  %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",        \
                  __FILE__, __LINE__, #cond);                                  \
      std::printf(__VA_ARGS__);                                                \
      std::printf("\n");                                                       \
    }                                                                          \
  } while (0)

static std::string gDir;

static std::string writeFile(const char *name, const std::vector<unsigned char> &data) {
  const std::string path = gDir + "/" + name;
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  return path;
}

static std::vector<unsigned char> pattern(size_t n) {
  std::vector<unsigned char> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<unsigned char>(i * 7 + 1);
  return v;
}

static void testDiskFile() {
  std::printf("DiskFile\n");
  const std::vector<unsigned char> data = pattern(100);
  const std::string path = writeFile("hundred.bin", data);

  {
    DiskFile f;
    CHECK(f.open((gDir + "/missing.bin").c_str()) == SoLoud::FILE_NOT_FOUND,
          "a missing file should give FILE_NOT_FOUND");
    unsigned char buf[8] = {0};
    CHECK(f.read(buf, 8) == 0 && f.length() == 0 && f.pos() == 0 && f.eof(),
          "without an open file it should act as an empty file");
    f.seek(10);
    CHECK(f.pos() == 0, "a seek without an open file should stay at 0");
    CHECK(f.open(nullptr) == SoLoud::INVALID_PARAMETER,
          "open(nullptr) should give INVALID_PARAMETER");
  }
  {
    DiskFile f;
    const std::string empty = writeFile("empty.bin", {});
    CHECK(f.open(empty.c_str()) == SoLoud::SO_NO_ERROR, "a 0-byte file opens");
    unsigned char buf[8];
    CHECK(f.length() == 0, "length of a 0-byte file: %u", f.length());
    CHECK(f.read(buf, 8) == 0, "a read of a 0-byte file gives nothing");
    CHECK(f.eof(), "after the read the file is at its end");
  }
  {
    DiskFile f;
    CHECK(f.open(path.c_str()) == SoLoud::SO_NO_ERROR, "open");
    CHECK(f.length() == 100, "length %u", f.length());
    unsigned char buf[64];
    f.seek(90);
    CHECK(f.read(buf, 50) == 10, "a read past the end gives the 10 bytes left");
    CHECK(std::memcmp(buf, data.data() + 90, 10) == 0, "and the right bytes");
    CHECK(f.pos() == 100 && f.eof(), "then pos %u is the end", f.pos());
    f.seek(200);
    CHECK(f.read(buf, 4) == 0, "a read after a seek past the end gives nothing");
    f.seek(20);
    CHECK(f.pos() == 20, "a seek back works after that: pos %u", f.pos());
    CHECK(f.read(buf, 4) == 4 && std::memcmp(buf, data.data() + 20, 4) == 0,
          "and reads the right bytes");
    f.seek(-10);
    // fseek to a negative offset fails and leaves the position; MemoryFile
    // counts negative offsets from the end instead.
    CHECK(f.pos() <= f.length(), "a negative seek leaves a valid position (%u)",
          f.pos());
    CHECK(f.length() == 100 && f.pos() == 24,
          "length() keeps the position: %u", f.pos());
    CHECK(f.read8() == data[24] && f.read16() == (unsigned)(data[25] | data[26] << 8),
          "read8/read16 read little-endian bytes");
    f.seek(99);
    CHECK(f.read32() == data[99], "read32 with one byte left gives that byte");
  }
  {
    // Deleted while open: POSIX keeps the data readable; Windows refuses to
    // delete an open file. Either way nothing crashes.
    const std::string doomed = writeFile("doomed.bin", data);
    DiskFile f;
    CHECK(f.open(doomed.c_str()) == SoLoud::SO_NO_ERROR, "open");
    std::error_code ec;
    std::filesystem::remove(doomed, ec);
    unsigned char buf[100];
    const unsigned int n = f.read(buf, 100);
    CHECK(n == 100 || n == 0, "a read of a deleted file gives %u bytes", n);
  }
  {
    // Truncated while open, with the position past the new end.
    const std::string shrinking = writeFile("shrinking.bin", data);
    DiskFile f;
    CHECK(f.open(shrinking.c_str()) == SoLoud::SO_NO_ERROR, "open");
    unsigned char buf[100];
    CHECK(f.read(buf, 50) == 50, "first read");
    std::error_code ec;
    std::filesystem::resize_file(shrinking, 10, ec);
    if (!ec) {
      // stdio may still hand out what it buffered before the truncation;
      // only check that the read stays within the buffer.
      CHECK(f.read(buf, 50) <= 50, "a read past the new end");
      CHECK(f.length() == 10, "length() sees the new size: %u", f.length());
      f.seek(0);
      CHECK(f.read(buf, 100) == 10, "the 10 bytes left are read");
    }
  }
  {
    // Reopening a DiskFile closes the first file.
    const std::string other = writeFile("other.bin", pattern(30));
    DiskFile f;
    CHECK(f.open(path.c_str()) == SoLoud::SO_NO_ERROR, "open");
    CHECK(f.open(other.c_str()) == SoLoud::SO_NO_ERROR, "reopen");
    CHECK(f.length() == 30, "the second file is open: %u", f.length());
    CHECK(f.open((gDir + "/missing.bin").c_str()) == SoLoud::FILE_NOT_FOUND,
          "reopen to a missing file");
    unsigned char buf[4];
    CHECK(f.read(buf, 4) == 0 && f.length() == 0 && f.eof(),
          "after a failed reopen the file acts as an empty one");
  }
  {
    FILE *fp = std::fopen(path.c_str(), "rb");
    DiskFile f(fp);
    CHECK(f.getFilePtr() == fp && f.length() == 100,
          "the FILE* constructor uses the stream");
  }
}

#if !defined(_WIN32)
/// Runs [fn] in a child process: true when it exits normally with status 0.
template <typename Fn> static bool survives(Fn fn) {
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    // The crash report of a sanitizer build is expected here: keep it out.
    std::freopen("/dev/null", "w", stderr);
    fn();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

static void testMemoryFile() {
  std::printf("MemoryFile\n");
  const std::vector<unsigned char> data = pattern(100);
  {
    MemoryFile m;
    CHECK(m.openMem(nullptr, 10, false, false) == SoLoud::INVALID_PARAMETER,
          "openMem(nullptr)");
    CHECK(m.openMem(data.data(), 0, false, false) == SoLoud::INVALID_PARAMETER,
          "openMem with no bytes");
    unsigned char buf[4];
    CHECK(m.length() == 0 && m.read(buf, 4) == 0 && m.eof(),
          "an unopened MemoryFile acts as an empty file");
    m.seek(5);
    CHECK(m.pos() == 0, "a seek of an empty MemoryFile stays at 0");
    m.seek(-5);
    CHECK(m.pos() == 0, "a negative seek of an empty MemoryFile stays at 0");
  }
  {
    MemoryFile m;
    CHECK(m.openMem(data.data(), 100, false, false) == SoLoud::SO_NO_ERROR,
          "openMem");
    unsigned char buf[128];
    m.seek(90);
    CHECK(m.read(buf, 50) == 10 && std::memcmp(buf, data.data() + 90, 10) == 0,
          "a read past the end gives the 10 bytes left");
    CHECK(m.pos() == 100 && m.eof(), "then at the end");
    CHECK(m.read(buf, 1) == 0, "a read at the end gives nothing");
    m.seek(0);
    CHECK(m.read(buf, 100) == 100 && std::memcmp(buf, data.data(), 100) == 0,
          "a read of exactly the length");
    m.seek(1000);
    CHECK(m.pos() == 100, "a seek past the end clamps to the end: %u", m.pos());
    m.seek(-10);
    CHECK(m.pos() == 90, "a negative seek counts from the end: %u", m.pos());
    m.seek(-100);
    CHECK(m.pos() == 0, "seek(-length) is the start: %u", m.pos());
    m.seek(-1000);
    CHECK(m.pos() == 0, "a negative seek past the start clamps to 0: %u",
          m.pos());
    m.seek(99);
    CHECK(m.read32() == data[99], "read32 with one byte left gives that byte");
    CHECK(m.read16() == 0 && m.read8() == 0, "reads at the end give 0");
  }
#if !defined(_WIN32)
  {
    // A byte count that wraps mOffset + aBytes past UINT_MAX: the bound
    // check `mOffset + aBytes >= mDataLength` sees a small sum and lets the
    // memcpy copy ~4 GB. Run in a child, which crashes when the bug is there.
    MemoryFile m;
    m.openMem(data.data(), 100, false, false);
    m.seek(10);
    const bool ok = survives([&] {
      std::vector<unsigned char> dst(200);
      const unsigned int n = m.read(dst.data(), 0xFFFFFFFFu - 5u);
      _exit(n == 90 ? 0 : 3);
    });
    CHECK(ok, "read(dst, 0xFFFFFFFA) at offset 10 of 100 bytes should give "
              "the 90 bytes left; the child crashed or read the wrong count");
  }
#endif
}

/// A File whose length() promises more bytes than read() gives, as a file
/// truncated between the two calls.
class ShortFile : public SoLoud::File {
public:
  unsigned int mLength, mReadable, mPos = 0;
  ShortFile(unsigned int length, unsigned int readable)
      : mLength(length), mReadable(readable) {}
  int eof() override { return mPos >= mReadable; }
  unsigned int read(unsigned char *aDst, unsigned int aBytes) override {
    const unsigned int n = mPos >= mReadable ? 0
                           : (aBytes < mReadable - mPos ? aBytes
                                                        : mReadable - mPos);
    std::memset(aDst, 0x5A, n);
    mPos += n;
    return n;
  }
  unsigned int length() override { return mLength; }
  void seek(int aOffset) override { mPos = aOffset < 0 ? 0 : aOffset; }
  unsigned int pos() override { return mPos; }
};

static void testToMem() {
  std::printf("MemoryFile::openToMem / openFileToMem\n");
  const std::vector<unsigned char> data = pattern(100);
  const std::string path = writeFile("tomem.bin", data);
  {
    MemoryFile m;
    CHECK(m.openToMem(path.c_str()) == SoLoud::SO_NO_ERROR, "openToMem");
    CHECK(m.length() == 100 && std::memcmp(m.getMemPtr(), data.data(), 100) == 0,
          "openToMem copies the file");
    CHECK(m.openToMem(nullptr) == SoLoud::INVALID_PARAMETER,
          "openToMem(nullptr)");
  }
  {
    MemoryFile m;
    const std::string empty = writeFile("tomem_empty.bin", {});
    const SoLoud::result r = m.openToMem(empty.c_str());
    unsigned char buf[4];
    CHECK(r != SoLoud::SO_NO_ERROR || (m.length() == 0 && m.read(buf, 4) == 0),
          "a 0-byte file: either refused or an empty MemoryFile");
  }
  {
    // A failed openToMem after a successful one: the old data was freed.
    MemoryFile m;
    CHECK(m.openToMem(path.c_str()) == SoLoud::SO_NO_ERROR, "first openToMem");
    const SoLoud::result r = m.openToMem((gDir + "/missing.bin").c_str());
    CHECK(r == SoLoud::FILE_NOT_FOUND, "a missing file: %d", (int)r);
    // The old data is freed before the file is opened: on failure the
    // MemoryFile must not claim the old length at a null pointer.
    const bool consistent = m.length() == 0 || m.getMemPtr() != nullptr;
    CHECK(consistent, "after a failed openToMem the file has length %u and "
                      "data pointer %p", m.length(), (const void *)m.getMemPtr());
    if (consistent) {
      unsigned char buf[8];
      CHECK(m.read(buf, 8) == 0, "and reads nothing");
    }
  }
  {
    // Truncated between length() and read().
    ShortFile src(100, 40);
    MemoryFile m;
    const SoLoud::result r = m.openFileToMem(&src);
    // Only the bytes actually read may be kept: an uninitialized tail would
    // be decoded as audio by Wav::loadFile.
    CHECK(r != SoLoud::SO_NO_ERROR || m.length() == 40,
          "a source that reads 40 of its 100 bytes gave result %d and length "
          "%u", (int)r, m.length());
  }
}

static void testFilehack() {
  std::printf("Soloud_Filehack\n");
  const std::vector<unsigned char> data = pattern(100);
  const std::string path = writeFile("hack.bin", data);
  CHECK(Soloud_Filehack_fopen((gDir + "/missing.bin").c_str(), nullptr) ==
            nullptr,
        "fopen of a missing file gives null");
  Soloud_Filehack *f = Soloud_Filehack_fopen(path.c_str(), nullptr);
  CHECK(f != nullptr, "fopen");
  if (!f) return;
  Soloud_Filehack_fseek(f, 10, SEEK_SET);
  CHECK(Soloud_Filehack_ftell(f) == 10, "SEEK_SET");
  Soloud_Filehack_fseek(f, 5, SEEK_CUR);
  CHECK(Soloud_Filehack_ftell(f) == 15, "SEEK_CUR");
  Soloud_Filehack_fseek(f, -4, SEEK_END);
  CHECK(Soloud_Filehack_ftell(f) == 96, "SEEK_END");
  unsigned char buf[16];
  CHECK(Soloud_Filehack_fread(buf, 2, 8, f) == 2,
        "fread of 8 items of 2 bytes with 4 bytes left gives 2 items");
  CHECK(Soloud_Filehack_fgetc(f) == EOF, "fgetc at the end gives EOF");
  Soloud_Filehack_fclose(f);
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i)
    if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
  gDir = makeTestTempDir("soloud_file_test");
  if (gDir.empty()) {
    std::printf("cannot make a temp directory\n");
    return 1;
  }
  testDiskFile();
  testMemoryFile();
  testToMem();
  testFilehack();
  std::error_code ec;
  std::filesystem::remove_all(gDir, ec);
  std::printf("\n%d checks, %d failures, %d known bugs\n", gChecks, gFailures,
              gKnownBugs);
  return gFailures ? 1 : 0;
}
