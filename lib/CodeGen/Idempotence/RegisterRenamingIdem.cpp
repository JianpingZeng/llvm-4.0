//===-- RegisterRenamingIdem.cpp - Performing Register Renaming -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define  DEBUG_TYPE "idem"

#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/Passes.h>
#include <llvm/CodeGen/Idempotence/RegisterUsesCollector.h>

using namespace llvm;

namespace {
class RegisterRenamingIdem : public MachineFunctionPass {
public:
  static char ID;
  RegisterRenamingIdem() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "Register renaming for Idempotence"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
};
char RegisterRenamingIdem::ID = 0;
}// end of annonymous namespace.

char& llvm::RegisterRenamingIdemID = RegisterRenamingIdem::ID;

INITIALIZE_PASS_BEGIN(RegisterRenamingIdem, "reg-renaming",
                     "Register Renaming for Idempotence", false, false)
INITIALIZE_PASS_DEPENDENCY(RegisterUsesCollector)
INITIALIZE_PASS_END(RegisterRenamingIdem, "reg-renaming",
                    "Register Renaming for Idempotence", false, false)

void RegisterRenamingIdem::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<RegisterUsesCollector>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool RegisterRenamingIdem::runOnMachineFunction(MachineFunction &MF) {
  return false;
}

FunctionPass *llvm::createRegisterRenamingPass() {
  return new RegisterRenamingIdem();
}