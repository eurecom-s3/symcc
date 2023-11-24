
#include "Tracer.h"
#include "Shadow.h"

nlohmann::json Tracer::currentTrace;
const std::string Tracer::BACKEND_TRACE_FILE = "/tmp/backend_trace.json";

void Tracer::trace(uintptr_t pc) {


  std::vector<uintptr_t> symbolicAddresses;

  for (auto const &[pageAddress, _] : g_shadow_pages) {
    for (auto byteAddress = pageAddress; byteAddress < pageAddress + kPageSize;
         byteAddress++) {
      auto byteExpr = _sym_read_memory((u_int8_t *)byteAddress, 1, true);
      if (byteExpr != nullptr && !byteExpr->isConcrete()) {
        symbolicAddresses.push_back(byteAddress);
      }
    }
  }

  nlohmann::json newEntry;
  newEntry["pc"] = pc;
  newEntry["symbolicAddresses"] = symbolicAddresses;

  currentTrace.push_back(newEntry);
}


void Tracer::writeTraceToDisk() {
  std::ofstream o(BACKEND_TRACE_FILE);
  o << std::setw(4) << currentTrace << std::endl;
}