
#ifndef SYMRUNTIME_TRACER_H
#define SYMRUNTIME_TRACER_H

#include "Runtime.h"
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>

class Tracer {
public:
  static void trace(uintptr_t pc);
  static void writeTraceToDisk();

private:
  static void recursivelyCollectSymbols(SymExpr symbol);
  static string getSymbolID(SymExpr symbol);

  static nlohmann::json currentTrace;
  static nlohmann::json symbols;
  static const std::string BACKEND_TRACE_FILE;
};

#endif // SYMRUNTIME_TRACER_H
