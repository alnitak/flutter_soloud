#ifndef ACTIVE_SOUND_H
#define ACTIVE_SOUND_H

#include "enums.h"
#include "filters/filters.h"
#include "soloud.h"

#include <iostream>
#include <vector>
#include <memory>

class Filters;

/// The default number of concurrent voices - maximum number of "streams" - is 16,
/// but this can be adjusted at runtime
typedef struct ActiveSound
{
    std::shared_ptr<SoLoud::AudioSource> sound;
    SoundType soundType;
    std::vector<SoLoud::handle> handle;
    std::unique_ptr<Filters> filters;
    // unique identifier of this sound based on the file name
    unsigned int soundHash;
    std::string completeFileName;
} ActiveSound;

#endif // ACTIVE_SOUND_H