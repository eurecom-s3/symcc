// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

#include "Config.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <sstream>
#include <stdexcept>

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
  auto *fullyConcrete = getenv("SYMCC_NO_SYMBOLIC_INPUT");
  if (fullyConcrete != nullptr)
    g_config.fullyConcrete = checkFlagString(fullyConcrete);

  auto *outputDir = getenv("SYMCC_OUTPUT_DIR");
  if (outputDir != nullptr)
    g_config.outputDir = outputDir;

  auto *inputFile = getenv("SYMCC_INPUT_FILE");
  if (inputFile != nullptr)
    g_config.inputFile = inputFile;

  auto *logFile = getenv("SYMCC_LOG_FILE");
  if (logFile != nullptr)
    g_config.logFile = logFile;

  auto *pruning = getenv("SYMCC_ENABLE_LINEARIZATION");
  if (pruning != nullptr)
    g_config.pruning = checkFlagString(pruning);

  auto *aflCoverageMap = getenv("SYMCC_AFL_COVERAGE_MAP");
  if (aflCoverageMap != nullptr)
    g_config.aflCoverageMap = aflCoverageMap;

  auto *garbageCollectionThreshold = getenv("SYMCC_GC_THRESHOLD");
  if (garbageCollectionThreshold != nullptr) {
    try {
      g_config.garbageCollectionThreshold =
          std::stoul(garbageCollectionThreshold);
    } catch (std::invalid_argument &) {
      std::stringstream msg;
      msg << "Can't convert " << garbageCollectionThreshold << " to an integer";
      throw std::runtime_error(msg.str());
    } catch (std::out_of_range &) {
      std::stringstream msg;
      msg << "The GC threshold must be between 0 and " << std::numeric_limits<size_t>::max();
      throw std::runtime_error(msg.str());
    }
  }
}
