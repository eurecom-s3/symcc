
#ifndef SYMRUNTIME_TRACER_H
#define SYMRUNTIME_TRACER_H

#include "GarbageCollection.h"
#include "Runtime.h"
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>

class Tracer {
public:
  static void traceStep(uintptr_t pc);
  static void tracePathConstraint(SymExpr constraint, bool taken);
  static void traceNewInput(const unsigned char* input, size_t size);
  static void writeTraceToDisk();

private:
  static void recursivelyCollectSymbols(const shared_ptr<qsym::Expr>& symbolPtr);
  static string getSymbolID(SymExpr symbol);

  static nlohmann::json currentTrace;
  static nlohmann::json symbols;
  static nlohmann::json pathConstraints;
  static const std::string BACKEND_TRACE_FILE;
};

#endif // SYMRUNTIME_TRACER_H
