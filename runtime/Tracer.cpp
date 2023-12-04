
#include "Tracer.h"
#include "Shadow.h"
#include "llvm/Support/raw_ostream.h"

nlohmann::json Tracer::currentTrace;
nlohmann::json Tracer::symbols;
nlohmann::json Tracer::pathConstraints;
const std::string Tracer::BACKEND_TRACE_FILE = "/tmp/backend_trace.json";

void Tracer::traceStep(uintptr_t pc) {

  nlohmann::json newEntry;
  newEntry["pc"] = pc;
  newEntry["memory_to_symbol_mapping"] = nlohmann::json::object();

  for (auto const &[pageAddress, _] : g_shadow_pages) {
    for (auto byteAddress = pageAddress; byteAddress < pageAddress + kPageSize;
         byteAddress++) {
      auto byteExpr = _sym_read_memory((u_int8_t *)byteAddress, 1, true);
      if (byteExpr != nullptr && !byteExpr->isConcrete()) {

        newEntry["memory_to_symbol_mapping"][std::to_string(reinterpret_cast<uintptr_t>(byteAddress))] =
                    getSymbolID(byteExpr);
      }
    }
  }

  for (auto &expressionRegion : getExpressionRegions()){
    for (auto byteAddress = expressionRegion.first; byteAddress < expressionRegion.first + expressionRegion.second / sizeof(byteAddress);
         byteAddress++) {
      auto byteExpr = *byteAddress;
      if (byteExpr != nullptr && !byteExpr->isConcrete()) {

        newEntry["memory_to_symbol_mapping"][std::to_string(reinterpret_cast<uintptr_t>(byteAddress))] =
            getSymbolID(byteExpr);
      }
    }
  }

  currentTrace.push_back(newEntry);
}


void Tracer::tracePathConstraint(SymExpr constraint) {
  if (pathConstraints.empty()) {
    symcc_set_test_case_handler(
        reinterpret_cast<TestCaseHandler>(traceNewInput));
  }

  nlohmann::json newEntry;
  newEntry["symbol"] = getSymbolID(constraint);
  newEntry["after_step"] = currentTrace.size() - 1;
  newEntry["new_input_value"] = nlohmann::json();

  pathConstraints.push_back(newEntry);
}

void Tracer::traceNewInput(const unsigned char *input, size_t size) {
  for (size_t i = 0; i < size; i++) {
    pathConstraints[pathConstraints.size() - 1]["new_input_value"].push_back(
        input[i]);
  }
}

void Tracer::writeTraceToDisk() {
  for (auto const &[_, symbolPtr] : getAllocatedExpressions()) {
    recursivelyCollectSymbols(symbolPtr);
  }

  nlohmann::json dataToSave;
  dataToSave["trace"] = currentTrace;
  dataToSave["symbols"] = symbols;
  dataToSave["path_constraints"] = pathConstraints;

  std::ofstream o(BACKEND_TRACE_FILE);
  o << std::setw(4) << dataToSave << std::endl;
}

void Tracer::recursivelyCollectSymbols(const shared_ptr<qsym::Expr>& symbolPtr) {
  string symbolID = getSymbolID(symbolPtr.get());
  if (symbols.count(symbolID) > 0) {
    return;
  }

  symbols[symbolID]["operation"]["kind"] = symbolPtr->kind();
  symbols[symbolID]["operation"]["properties"] = nlohmann::json::object();
  if (symbolPtr->kind() == qsym::Constant){
    auto value_llvm_int = static_pointer_cast<qsym::ConstantExpr>(symbolPtr)->value();
    std::string value_str;
    llvm::raw_string_ostream rso(value_str);
    value_llvm_int.print(rso, false);

    symbols[symbolID]["operation"]["properties"]["value"] = value_str;
  }
  symbols[symbolID]["size_bits"] = symbolPtr->bits();
  symbols[symbolID]["input_byte_dependency"] = *symbolPtr->getDependencies();
  symbols[symbolID]["args"] = nlohmann::json::array();
  for (int child_i = 0; child_i < symbolPtr->num_children(); child_i++) {
    shared_ptr<qsym::Expr> child = symbolPtr->getChild(child_i);
    string childID = getSymbolID(child.get());
    symbols[symbolID]["args"].push_back(childID);
    recursivelyCollectSymbols(child);
  }
}

string Tracer::getSymbolID(SymExpr symbol) {
  return std::to_string(reinterpret_cast<uintptr_t>(symbol));
}