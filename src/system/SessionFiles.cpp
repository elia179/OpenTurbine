#include "SessionFiles.h"

#include <climits>
#include <cstring>

namespace SessionFiles {

bool parseRunNumber(const char* path, int& runNumber) {
    if (!path) return false;

    const char* slash = strrchr(path, '/');
    const char* text = slash ? slash + 1 : path;
    static constexpr char PREFIX[] = "session_";
    static constexpr char SUFFIX[] = ".csv";

    if (strncmp(text, PREFIX, sizeof(PREFIX) - 1) != 0) return false;
    text += sizeof(PREFIX) - 1;
    if (*text < '0' || *text > '9') return false;

    unsigned value = 0;
    do {
        const unsigned digit = static_cast<unsigned>(*text - '0');
        if (value > (static_cast<unsigned>(INT_MAX) - digit) / 10U) return false;
        value = value * 10U + digit;
        ++text;
    } while (*text >= '0' && *text <= '9');

    if (strcmp(text, SUFFIX) != 0) return false;
    runNumber = static_cast<int>(value);
    return true;
}

}  // namespace SessionFiles
