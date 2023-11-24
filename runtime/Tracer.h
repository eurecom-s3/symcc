
#ifndef SYMRUNTIME_TRACER_H
#define SYMRUNTIME_TRACER_H

#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>

class Tracer {
public:
  static void trace(uintptr_t pc);
  static void writeTraceToDisk();

private:
  static nlohmann::json currentTrace;
  static const std::string BACKEND_TRACE_FILE;
};

#endif // SYMRUNTIME_TRACER_H
