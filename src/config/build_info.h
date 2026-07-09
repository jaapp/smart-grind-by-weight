#pragma once

#include <cstdio>
#include "../../include/git_info.h"

// Build information - BUILD_FIRMWARE_VERSION is automatically updated by release scripts
#define BUILD_FIRMWARE_VERSION "2.4.0"                                          // Firmware version string (updated by release automation)
// BUILD_NUMBER is now defined in git_info.h as an auto-incrementing integer
#define BUILD_DATE __DATE__                                                // Compiler-provided build date
#define BUILD_TIME __TIME__                                                // Compiler-provided build time

// Fallback in case git_info.h predates the GIT_COMMIT_AUTHOR field (regenerated each build)
#ifndef GIT_COMMIT_AUTHOR
#define GIT_COMMIT_AUTHOR "unknown"
#endif

// Product identity (shown on the About page). A local, uncommitted override can be
// placed in src/config/product_identity.local.h (gitignored) defining PRODUCT_NAME
// and/or PRODUCT_MODEL — handy for personalizing your own device without committing
// third-party branding to the repo.
#if defined(__has_include)
#  if __has_include("product_identity.local.h")
#    include "product_identity.local.h"
#  endif
#endif
#ifndef PRODUCT_NAME
#define PRODUCT_NAME "Neo GBW"                                                // Product/device name
#endif
#ifndef PRODUCT_MODEL
#define PRODUCT_MODEL "for Eureka Mignon Filtro"                              // Hardware model this build targets
#endif
#define ORIGINAL_AUTHOR_CREDIT "Based on smart-grind-by-weight by jaapp; grind-by-weight concept from openGBW by jb-xyz"

inline const char* get_git_commit_id() {
    return GIT_COMMIT_ID;
}

inline const char* get_git_branch() {
    return GIT_BRANCH;
}

inline const char* get_git_commit_author() {
    return GIT_COMMIT_AUTHOR;
}

inline const char* get_build_datetime() {
    static char datetime[32];
    snprintf(datetime, sizeof(datetime), "%s %s", __DATE__, __TIME__);
    return datetime;
}
