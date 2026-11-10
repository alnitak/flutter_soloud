#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

#include "mixer_output.h"

/// Delivery of capture packet tokens to a Dart isolate's native port.
///
/// A NativeCallable belongs to the isolate that created it, and invoking one
/// after that isolate has ended aborts the VM. A native port does not have
/// that problem: Dart_PostInteger belongs to the VM, and posting to a port
/// whose isolate is gone just fails. It is resolved from the table
/// `NativeApi.initializeApiDLData` hands over, so no SDK sources are compiled
/// in.
namespace mixer_output_port {

using PostInteger = bool (*)(int64_t port, int64_t message);

/// Dart_PostInteger from the API table at [data], or nullptr when [data] is
/// not a table of the layout this reads: major version 2 of the SDK's
/// include/internal/dart_api_dl_impl.h, a name-to-function list ending in a
/// null name.
inline PostInteger resolve(const void *data) {
  struct Entry {
    const char *name;
    void (*function)();
  };
  struct Api {
    int major;
    int minor;
    const Entry *functions;
  };
  if (data == nullptr) {
    return nullptr;
  }
  const Api &api = *static_cast<const Api *>(data);
  if (api.major != 2 || api.functions == nullptr) {
    return nullptr;
  }
  for (const Entry *entry = api.functions; entry->name != nullptr; ++entry) {
    if (std::strcmp(entry->name, "Dart_PostInteger") == 0) {
      return reinterpret_cast<PostInteger>(entry->function);
    }
  }
  return nullptr;
}

/// Post the packet [token] to [port]. A post that fails means the port is
/// closed -- its isolate returned, exited or was killed -- so nobody can
/// claim the packet: it is discarded and false returned.
inline bool send(PostInteger post, int64_t port, uint8_t *token) {
  const uintptr_t value = reinterpret_cast<uintptr_t>(token);
  if (value == 0) {
    return true;  // no packet, and nothing wrong with the port
  }
  if (post == nullptr || port == 0 ||
      value > static_cast<uintptr_t>(std::numeric_limits<int64_t>::max()) ||
      !post(port, static_cast<int64_t>(value))) {
    MixerOutput::discardPacket(token);
    return false;
  }
  return true;
}

}  // namespace mixer_output_port
