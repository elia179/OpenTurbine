#pragma once

namespace SessionFiles {

// Parse "session_<non-negative decimal>.csv". The input may be either a
// basename or a LittleFS path such as "/logs/session_42.csv".
bool parseRunNumber(const char* path, int& runNumber);

}  // namespace SessionFiles
