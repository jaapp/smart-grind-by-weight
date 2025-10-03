#pragma once

#include <cstdio>
#include "../../include/git_info.h"

// Build information - BUILD_FIRMWARE_VERSION is automatically updated by release scripts
#define BUILD_FIRMWARE_VERSION "1.3.0-rc.18"                                          // Firmware version string (updated by release automation)
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
