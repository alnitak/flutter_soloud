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

    // Add explicit destructor to control cleanup order
    ~ActiveSound() {
        try {
            printf("CPP ~ActiveSound1\n");
            // Clear handles first
            handle.clear();
            
            printf("CPP ~ActiveSound2\n");
            // Reset filters before sound since filters may depend on sound
            if (filters) {
                printf("CPP ~ActiveSound2A\n");
                Filters *f = filters.release();
                printf("CPP ~ActiveSound2B %p\n", f);
                delete f;
                printf("CPP ~ActiveSound2C\n");
                // filters.reset();
            }
            
            printf("CPP ~ActiveSound3\n");
            // Finally reset sound
            if (sound) {
                sound->stop();
                sound.reset();
            }
            printf("CPP ~ActiveSound4\n");
        }
        catch (const std::exception& e) {
            printf("Error in ActiveSound destructor: %s\n", e.what());
        }
        catch (...) {
            printf("Unknown error in ActiveSound destructor\n");
        }
        printf("CPP ~ActiveSound5\n");
    }
};

#endif // ACTIVE_SOUND_H