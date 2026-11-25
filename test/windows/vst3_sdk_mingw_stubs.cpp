// The two pieces of the VST3 SDK's hosting library that
// src/vst3/vst3_editor_windows.cpp needs at link time, for the mingw-w64 build
// of vst3_editor_windows_test under Wine (test/docker/windows_native.sh): the
// prebuilt vst3_hosting.lib is an MSVC library mingw cannot link. The MSVC
// build (vst3_editor_windows_test.ps1) links the real library instead.
//
//   - the interface IDs, defined here by INIT_CLASS_IID, and
//     FUnknownPrivate::atomicAdd (pluginterfaces/base/funknown.cpp);
//   - Steinberg::MemoryStream (public.sdk/source/common/memorystream.cpp),
//     used only by the out-of-process editor path, which the test does not
//     reach: a minimal growable buffer.

#define INIT_CLASS_IID
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstdlib>
#include <cstring>

namespace Steinberg {

// pluginterfaces/base/funknown.cpp: the reference counting helper.
namespace FUnknownPrivate {
int32 PLUGIN_API atomicAdd(int32 &value, int32 amount)
{
    return __atomic_add_fetch(&value, amount, __ATOMIC_SEQ_CST);
}
} // namespace FUnknownPrivate


MemoryStream::MemoryStream()
    : memory(nullptr), memorySize(0), size(0), cursor(0), ownMemory(true), allocationError(false)
{
    FUNKNOWN_CTOR
}
MemoryStream::MemoryStream(void *data, TSize length)
    : memory(static_cast<char *>(data)), memorySize(length), size(length), cursor(0), ownMemory(false),
      allocationError(false)
{
    FUNKNOWN_CTOR
}
MemoryStream::~MemoryStream()
{
    if (ownMemory) std::free(memory);
    FUNKNOWN_DTOR
}
IMPLEMENT_FUNKNOWN_METHODS(MemoryStream, IBStream, IBStream::iid)

tresult PLUGIN_API MemoryStream::read(void *buffer, int32 numBytes, int32 *numBytesRead)
{
    int64 n = numBytes;
    if (cursor + n > size) n = size - cursor;
    if (n < 0) n = 0;
    if (n > 0) std::memcpy(buffer, memory + cursor, static_cast<size_t>(n));
    cursor += n;
    if (numBytesRead) *numBytesRead = static_cast<int32>(n);
    return kResultTrue;
}
tresult PLUGIN_API MemoryStream::write(void *buffer, int32 numBytes, int32 *numBytesWritten)
{
    if (allocationError || !ownMemory) return kOutOfMemory;
    if (cursor + numBytes > memorySize) setSize(static_cast<TSize>(cursor + numBytes));
    if (allocationError) return kOutOfMemory;
    std::memcpy(memory + cursor, buffer, static_cast<size_t>(numBytes));
    cursor += numBytes;
    if (cursor > size) size = static_cast<TSize>(cursor);
    if (numBytesWritten) *numBytesWritten = numBytes;
    return kResultTrue;
}
tresult PLUGIN_API MemoryStream::seek(int64 pos, int32 mode, int64 *result)
{
    if (mode == kIBSeekCur) pos += cursor;
    else if (mode == kIBSeekEnd) pos += size;
    if (pos < 0) pos = 0;
    cursor = pos;
    if (result) *result = cursor;
    return kResultTrue;
}
tresult PLUGIN_API MemoryStream::tell(int64 *pos)
{
    if (!pos) return kInvalidArgument;
    *pos = cursor;
    return kResultTrue;
}
TSize MemoryStream::getSize() const { return size; }
void MemoryStream::setSize(TSize s)
{
    if (!ownMemory) return;
    char *grown = static_cast<char *>(std::realloc(memory, static_cast<size_t>(s ? s : 1)));
    if (!grown)
    {
        allocationError = true;
        return;
    }
    memory = grown;
    memorySize = s;
    if (size > s) size = s;
}
char *MemoryStream::getData() const { return memory; }
char *MemoryStream::detachData()
{
    char *d = memory;
    memory = nullptr;
    memorySize = size = 0;
    cursor = 0;
    return d;
}
bool MemoryStream::truncate() { return true; }
bool MemoryStream::truncateToCursor()
{
    size = static_cast<TSize>(cursor);
    return true;
}

} // namespace Steinberg
