// Stand-in for soloud_vst3_editor_helper in the native host tests
// (test/support/vst3_host_checks.h): it plays the helper's side of the
// out-of-process editor protocol without opening a window.
//
//   vst3_mock_editor_helper <bundle> <classUid> <title> <shm> <parentPid>
//
// <shm> is the shared memory the host made: a file path on macOS and Linux
// (mmap), a named file mapping on Windows (Local\SoLoudVst3Shm_...).
//
// It maps the host's shared memory, reports itself ready and waits for the
// host to close the editor. With the title "helper-closes" it closes the
// editor itself after a few milliseconds, as when the user closes the window.

#include "vst3/vst3_editor_shm.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
struct Mapping {
  HANDLE handle = nullptr;
  HANDLE parent = nullptr;
  void *view = nullptr;
};

void *mapShm(const char *name, Mapping &m) {
  m.handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
  if (m.handle == nullptr) return nullptr;
  m.view = MapViewOfFile(m.handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SoLoud::SoLoudVst3EditorShm));
  return m.view;
}

void unmapShm(Mapping &m) {
  if (m.view) UnmapViewOfFile(m.view);
  if (m.handle) CloseHandle(m.handle);
  if (m.parent) CloseHandle(m.parent);
}

bool parentAlive(Mapping &m, long pid) {
  if (m.parent == nullptr) {
    m.parent = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (m.parent == nullptr) return false;
  }
  return WaitForSingleObject(m.parent, 0) == WAIT_TIMEOUT;
}
#else
struct Mapping {
  void *view = nullptr;
};

void *mapShm(const char *path, Mapping &m) {
  const int fd = open(path, O_RDWR);
  if (fd < 0) return nullptr;
  void *mapped = mmap(nullptr, sizeof(SoLoud::SoLoudVst3EditorShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) return nullptr;
  m.view = mapped;
  return mapped;
}

void unmapShm(Mapping &m) {
  if (m.view) munmap(m.view, sizeof(SoLoud::SoLoudVst3EditorShm));
}

bool parentAlive(Mapping &, long pid) { return getppid() == static_cast<pid_t>(pid); }
#endif

} // namespace

int main(int argc, char **argv) {
  if (argc < 6) {
    return 2;
  }
  const bool helperCloses = std::strcmp(argv[3], "helper-closes") == 0;
  const long parent = std::atol(argv[5]);

  Mapping mapping;
  void *mapped = mapShm(argv[4], mapping);
  if (mapped == nullptr) {
    unmapShm(mapping);
    return 3;
  }
  auto *shm = static_cast<SoLoud::SoLoudVst3EditorShm *>(mapped);
  shm->helperReady.store(1, std::memory_order_release);

  const auto start = std::chrono::steady_clock::now();
  while (shm->hostClosed.load(std::memory_order_acquire) == 0 && parentAlive(mapping, parent)) {
    const auto elapsed = std::chrono::steady_clock::now() - start;
    if (helperCloses && elapsed > std::chrono::milliseconds(5)) {
      shm->helperClosed.store(1, std::memory_order_release);
      break;
    }
    if (elapsed > std::chrono::seconds(30)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  unmapShm(mapping);
  return 0;
}
