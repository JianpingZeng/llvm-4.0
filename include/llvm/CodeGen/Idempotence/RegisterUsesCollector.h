//===-- UseInfoCollector.h - Collect Register use information -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This file contains a class definition "RegisterUsesCollector" which is used for
// collecting register used by previous instructions for each machine instruction.
//
// RegAlloc can use this information to guarantee RegAlloc will not assign definition
// register and previous use register with a same physical register.

#ifndef LLVM_REGISTER_USE_COLLECTOR
#define LLVM_REGISTER_USE_COLLECTOR

#include <llvm/CodeGen/Passes.h>
#include <llvm/PassSupport.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/VirtRegMap.h>
#include <set>
#include <map>
#include <vector>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetInstrInfo.h>

namespace llvm {

/// This is analysis pass for collecting register uses.
/// This pass must run after all requried passes by RegAlloca.
/// @author JianpingZeng.
class RegisterUsesCollector : public MachineFunctionPass {
public:
  typedef std::map<MachineInstr*, std::set<int>> RegUses;

  static char ID;

  RegUses UseIns, UseOuts;
  const TargetRegisterInfo *tri;
  const TargetInstrInfo *tii;
  std::vector<MachineBasicBlock*> reverseOrderSequences;

  RegisterUsesCollector() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override
  { return "Register uses information collector"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool isPhyRegUsedBeforeMI(MachineInstr *mi,
                            int phyReg,
                            VirtRegMap *vrm,
                            const TargetRegisterInfo *tri);

private:
  void reversePostOrder(MachineBasicBlock* entry,
                        std::vector<MachineBasicBlock*> &res);

  void traverse(MachineBasicBlock* entry,
                std::vector<MachineBasicBlock*> &res,
                std::set<MachineBasicBlock*> &visited);

  void computeLocalDefUses(MachineInstr* mi,
                           std::set<int>& defs,
                           std::set<int> &uses);
  /**
   * performs intersect operation over lhs and rhs. Stores result
   * into lhs.
   * @param lhs
   * @param rhs
   */
  void intersect(std::set<int> &res, std::set<int> &lhs, std::set<int> &rhs);
  void Union(std::set<int> &res, std::set<int> &lhs, std::set<int> &rhs);
  void dump();
  void printSet(MachineInstr*, std::set<int>&);
};
}

#endif