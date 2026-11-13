// Stand-in for soloud_vst3_editor_helper in the native host tests
// (run_vst3_host_test.sh): it plays the helper's side of the out-of-process
// editor protocol without opening a window.
//
//   vst3_mock_editor_helper <bundle> <classUid> <title> <shmPath> <parentPid>
//
// It maps the host's shared memory, reports itself ready and waits for the
// host to close the editor. With the title "helper-closes" it closes the
// editor itself after a few milliseconds, as when the user closes the window.

#include "vst3/vst3_editor_shm.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 6) {
    return 2;
  }
  const bool helperCloses = std::strcmp(argv[3], "helper-closes") == 0;
  const pid_t parent = static_cast<pid_t>(std::atoi(argv[5]));

  const int fd = open(argv[4], O_RDWR);
  if (fd < 0) {
    return 3;
  }
  void *mapped = mmap(nullptr, sizeof(SoLoud::SoLoudVst3EditorShm),
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) {
    return 4;
  }
  auto *shm = static_cast<SoLoud::SoLoudVst3EditorShm *>(mapped);
  shm->helperReady.store(1, std::memory_order_release);

  const auto start = std::chrono::steady_clock::now();
  while (shm->hostClosed.load(std::memory_order_acquire) == 0 &&
         getppid() == parent) {
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
  munmap(mapped, sizeof(SoLoud::SoLoudVst3EditorShm));
  return 0;
}
