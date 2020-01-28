#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct Config {
  /// Should we allow symbolic data in the program?
  bool fullyConcrete = false;

  /// The directory for storing new outputs.
  std::string outputDir = "/tmp/output";

  /// The input file, if any.
  std::string inputFile;

  /// Do we prune expressions on hot paths?
  bool pruning = false;

  /// The AFL coverage map to initialize with.
  ///
  /// Specifying a file name here allows us to track already covered program
  /// locations across multiple program executions.
  std::string aflCoverageMap = "";
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
