#ifndef CONFIG_H
#define CONFIG_H

struct Config {
  bool fullyConcrete;
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
