#pragma once
#include <string>

namespace forts {

// Validates the build, then hooks gdi32!SwapBuffers in the game's import table. Nothing is hooked
// unless every check passes. The hook stays installed for the life of the process.
bool InstallFeatures(std::string& error);

}  // namespace forts
