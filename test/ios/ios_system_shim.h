// Force-included (-include) when a host test that shells out to afconvert is
// built for the iOS simulator, where system() does not exist.
//
// The script makes the files those commands would make on the host first, so
// this stand-in only reports whether the command's output (its last
// single-quoted argument) is there: 0 if it exists, 127 otherwise.
//
// simctl spawn also starts the process in /, while host tests open fixtures
// relative to the repo root: when SOLOUD_TEST_CWD is set, a constructor
// changes to it before main().
//
// Used by test/ios/native_decoder_test.sh.

#ifndef IOS_SYSTEM_SHIM_H
#define IOS_SYSTEM_SHIM_H

#include <unistd.h>

#ifdef __cplusplus
#include <cstdio>
#include <cstdlib>
#include <cstring>
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

static inline int soloud_ios_test_system(const char *command) {
    if (command == NULL) return 0;
    const char *end = strrchr(command, '\'');
    if (end == NULL || end == command) return 127;
    const char *start = end - 1;
    while (start > command && *start != '\'') --start;
    if (*start != '\'') return 127;
    char path[1024];
    const size_t length = (size_t)(end - start - 1);
    if (length == 0 || length >= sizeof(path)) return 127;
    memcpy(path, start + 1, length);
    path[length] = '\0';
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 127;
    fclose(file);
    return 0;
}

__attribute__((constructor)) static void soloud_ios_test_chdir(void) {
    const char *dir = getenv("SOLOUD_TEST_CWD");
    if (dir != NULL && dir[0] != '\0' && chdir(dir) != 0) perror("chdir(SOLOUD_TEST_CWD)");
}

#ifdef __cplusplus
namespace std {
using ::soloud_ios_test_system;
}
#endif

#define system soloud_ios_test_system

#endif // IOS_SYSTEM_SHIM_H
