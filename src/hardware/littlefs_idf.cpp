// LittleFS singleton definition.
// The LittleFSClass is declared in include/littlefs_idf.h; this translation
// unit provides the one-and-only global instance so that all other TUs that
// use `extern LittleFSClass LittleFS;` resolve to the same object.

#include "../../include/littlefs_idf.h"

LittleFSClass LittleFS;
