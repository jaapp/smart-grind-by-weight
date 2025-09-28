#pragma once

#include <cstdio>
#include "../../include/git_info.h"

// Build information - update these manually or use build scripts
#define FIRMWARE_VERSION "1.0.0"                                          // Firmware version string
#define INTERNAL_FIRMWARE_VERSION "1.0.0"                                 // Internal firmware version (for compatibility)
// BUILD_NUMBER is now defined in git_info.h as an auto-incrementing integer
#define BUILD_DATE __DATE__                                                // Compiler-provided build date
#define BUILD_TIME __TIME__                                                // Compiler-provided build time

inline const char* get_git_commit_id() {
    return GIT_COMMIT_ID;
}

inline const char* get_git_branch() {
    return GIT_BRANCH;
}

inline const char* get_build_datetime() {
    static char datetime[32];
    snprintf(datetime, sizeof(datetime), "%s %s", __DATE__, __TIME__);
    return datetime;
}
