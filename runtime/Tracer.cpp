
#include "Tracer.h"
#include "Shadow.h"
#include "llvm/Support/raw_ostream.h"

nlohmann::json Tracer::currentTrace;
nlohmann::json Tracer::expressions;
nlohmann::json Tracer::pathConstraints;
const std::string Tracer::BACKEND_TRACE_FILE = "/tmp/backend_trace.json";

void Tracer::traceStep(uintptr_t pc) {

  nlohmann::json newEntry;
  newEntry["pc"] = pc;
  newEntry["memory_to_expression_mapping"] = nlohmann::json::object();

  /* dump shadow pages */
  for (auto const &[pageAddress, _] : g_shadow_pages) {
    for (auto byteAddress = pageAddress; byteAddress < pageAddress + kPageSize;
         byteAddress++) {
      auto byteExpr = _sym_read_memory((u_int8_t *)byteAddress, 1, true);
      if (byteExpr != nullptr && !byteExpr->isConcrete()) {

        newEntry["memory_to_expression_mapping"][std::to_string(reinterpret_cast<uintptr_t>(byteAddress))] =
                    getExpressionID(byteExpr);
      }
    }
  }

  /* dump regions that the client has registered with _sym_register_expression_region
   * for SymQEMU, this is env_exprs which contains the expressions for the global TCG variables (i.e., guest registers) */
  for (auto &expressionRegion : getExpressionRegions()){
    for (auto byteAddress = expressionRegion.first; byteAddress < expressionRegion.first + expressionRegion.second / sizeof(byteAddress);
         byteAddress++) {
      auto byteExpr = *byteAddress;
      if (byteExpr != nullptr && !byteExpr->isConcrete()) {

        newEntry["memory_to_expression_mapping"][std::to_string(reinterpret_cast<uintptr_t>(byteAddress))] =
                    getExpressionID(byteExpr);
      }
    }
  }

  currentTrace.push_back(newEntry);
}


void Tracer::tracePathConstraint(SymExpr constraint, bool taken) {
  if (pathConstraints.empty()) {
     symcc_set_test_case_handler(
        reinterpret_cast<TestCaseHandler>(traceNewInput));
  }

  nlohmann::json newEntry;
  newEntry["expression"] = getExpressionID(constraint);
  newEntry["after_step"] = currentTrace.size() - 1;
  newEntry["new_input_value"] = nlohmann::json();
  newEntry["taken"] = taken;

  pathConstraints.push_back(newEntry);
}

void Tracer::traceNewInput(const unsigned char *input, size_t size) {
  for (size_t i = 0; i < size; i++) {
    pathConstraints[pathConstraints.size() - 1]["new_input_value"].push_back(
        input[i]);
  }
}

void Tracer::writeTraceToDisk() {
  for (auto const &[_, expressionPtr] : getAllocatedExpressions()) {
    recursivelyCollectExpressions(expressionPtr);
  }

  nlohmann::json dataToSave;
  dataToSave["trace"] = currentTrace;
  dataToSave["expressions"] = expressions;
  dataToSave["path_constraints"] = pathConstraints;

  std::ofstream o(BACKEND_TRACE_FILE);
  o << std::setw(4) << dataToSave << std::endl;
}

void Tracer::recursivelyCollectExpressions(const shared_ptr<qsym::Expr>&expressionPtr) {
  string expressionID = getExpressionID(expressionPtr.get());
  if (expressions.count(expressionID) > 0) {
    return;
  }

  expressions[expressionID]["operation"]["kind"] = expressionPtr->kind();
  expressions[expressionID]["operation"]["properties"] = nlohmann::json::object();
  if (expressionPtr->kind() == qsym::Constant){
    auto value_llvm_int = static_pointer_cast<qsym::ConstantExpr>(expressionPtr)->value();
    std::string value_str;
    llvm::raw_string_ostream rso(value_str);
    value_llvm_int.print(rso, false);

    expressions[expressionID]["operation"]["properties"]["value"] = value_str;
  }
  expressions[expressionID]["size_bits"] = expressionPtr->bits();
  expressions[expressionID]["input_byte_dependency"] = *expressionPtr->getDependencies();
  expressions[expressionID]["args"] = nlohmann::json::array();
  for (int child_i = 0; child_i < expressionPtr->num_children(); child_i++) {
    shared_ptr<qsym::Expr> child = expressionPtr->getChild(child_i);
    string childID = getExpressionID(child.get());
    expressions[expressionID]["args"].push_back(childID);
    recursivelyCollectExpressions(child);
  }
}

string Tracer::getExpressionID(SymExpr expression) {
  return std::to_string(reinterpret_cast<uintptr_t>(expression));
}
