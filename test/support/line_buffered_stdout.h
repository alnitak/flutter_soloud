// Makes stdout line buffered, so a test's progress shows up as it runs and is
// not lost when the process dies.
//
// The Microsoft C runtime has no line buffering (_IOLBF means full buffering)
// and rejects a size of 0 with _IOLBF as an invalid parameter, which ends the
// process (0xc0000409) before the test prints anything. There stdout is made
// unbuffered instead.
#pragma once

#include <cstdio>

inline void lineBufferedStdout()
{
#ifdef _WIN32
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
}
