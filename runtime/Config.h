// This file is part of the SymCC runtime.
//
// The SymCC runtime is free software: you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// The SymCC runtime is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the SymCC runtime. If not, see <https://www.gnu.org/licenses/>.

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <variant>

/// Marker struct for fully concrete execution.
struct NoInput {};

/// Marker struct for symbolic input from stdin.
struct StdinInput {};

/// Marker struct for symbolic input via _sym_make_symbolic.
struct MemoryInput {};

/// Configuration for symbolic input from a file.
struct FileInput {
  /// The name of input file.
  std::string fileName;
};

struct Config {
  using InputConfig = std::variant<NoInput, StdinInput, MemoryInput, FileInput>;

  /// The configuration for our symbolic input.
  InputConfig input = StdinInput{};

  /// The directory for storing new outputs.
  std::string outputDir = "/tmp/output";

  /// The file to log constraint solving information to.
  std::string logFile = "";

  /// Do we prune expressions on hot paths?
  bool pruning = false;

  /// The AFL coverage map to initialize with.
  ///
  /// Specifying a file name here allows us to track already covered program
  /// locations across multiple program executions.
  std::string aflCoverageMap = "";

  /// The garbage collection threshold.
  ///
  /// We will start collecting unused symbolic expressions if the total number
  /// of allocated expressions in the target program exceeds this number.
  ///
  /// Collecting too often hurts performance, whereas delaying garbage
  /// collection for too long might make us run out of memory. The goal of this
  /// empirically determined constant is to keep peek memory consumption below
  /// 2GB on most workloads because requiring that amount of memory per core
  /// participating in the analysis seems reasonable.
  size_t garbageCollectionThreshold = 5'000'000;
};

/// The global configuration object.
///
/// It should be initialized once before we start executing the program and
/// never changed afterwards.
extern Config g_config;

/// Populate g_config from the environment.
///
/// The function will throw std::runtime_error if the value of an environment
/// variable used for configuration cannot be interpreted.
void loadConfig();

#endif
