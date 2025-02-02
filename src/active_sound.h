#ifndef ACTIVE_SOUND_H
#define ACTIVE_SOUND_H

#include "filters/filters_fwd.h"
#include "enums.h"
#include "soloud.h"

#include <iostream>
#include <vector>
#include <memory>

#define MAX_DOUBLE 1.7976931348623157e+308

struct ActiveHandle
{
    SoLoud::handle handle;
    // this is managed only in `audiobuffer.cpp` an it is used to
    // know when a handle reaches the end or when there is not enough data 
    // in the buffer to resume playing. Used also to send the buffering events.
    SoLoud::time bufferingTime;
};

/// The default number of concurrent voices - maximum number of "streams" - is 16,
/// but this can be adjusted at runtime
struct ActiveSound
{
    std::unique_ptr<SoLoud::AudioSource> sound;
    SoundType soundType;
    std::vector<ActiveHandle> handle;
    std::unique_ptr<Filters> filters;
    // unique identifier of this sound based on the file name
    unsigned int soundHash;
    std::string completeFileName;
};

#endif // ACTIVE_SOUND_H