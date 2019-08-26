#include "Config.h"

#include <algorithm>
#include <sstream>

namespace {

bool checkFlagString(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (value == "1" || value == "on" || value == "yes")
    return true;

  if (value.empty() || value == "0" || value == "off" || value == "no")
    return false;

  std::stringstream msg;
  msg << "Unknown flag value " << value;
  throw std::runtime_error(msg.str());
}

} // namespace

Config g_config;

void loadConfig() {
  auto fullyConcrete = getenv("SYMCC_NO_SYMBOLIC_INPUT");
  g_config.fullyConcrete =
      (fullyConcrete == nullptr) ? false : checkFlagString(fullyConcrete);

  auto outputDir = getenv("SYMCC_OUTPUT_DIR");
  g_config.outputDir = (outputDir == nullptr) ? "/tmp/output" : outputDir;
}
