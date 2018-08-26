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
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/CodeGen/MachineRegisterInfo.h>
#include <vector>
#include <map>

using namespace llvm;

namespace {
class RegisterRenamingIdem : public MachineFunctionPass {
public:
  static char ID;
  RegisterUsesCollector *regUses;
  std::map<int, int> old2NewRegsMap;
  RegisterRenamingIdem() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "Register renaming for Idempotence"; }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
private:
  const TargetRegisterInfo* tri;
  MachineFunction *mf;
  std::vector<MachineBasicBlock*> seqs;
  bool renameRegsOverMI(MachineInstr*mi, MachineInstr* prev);
  bool shouldRenameDefReg(int defReg, std::set<int> usesIn);
  void updateUsedRegBy(MachineInstr* mi, unsigned oldReg, unsigned newReg);
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
/**
 * Currently, we implement register renaming upon functional level. In theory,
 * we should rename all anti-dependence registers over idempotence-region. However,
 * in order to simplify developement and test, functional level is good.
 * @param MF The machine function which this transformation will be operated on.
 * @return Return true if this function has been changed. Otherwise return false.
 */
bool RegisterRenamingIdem::runOnMachineFunction(MachineFunction &MF) {
  if (MF.empty()) return false;
  mf = &MF;
  regUses = &getAnalysis<RegisterUsesCollector>();
  assert(regUses && "RegisterUsesCollector must be available!");
  tri = MF.getSubtarget().getRegisterInfo();
  MachineBasicBlock *entry = &MF.front();
  RegisterUsesCollector::reversePostOrder(entry, seqs);

  bool changed = false;
  for (MachineBasicBlock* mbb : seqs) {
    if (!mbb)
      continue;
    auto mi = mbb->begin();
    auto end = mbb->end();
    MachineInstr *prev = 0;
    for (; mi != end; ++mi) {
      changed |= renameRegsOverMI(&*mi, prev);
      prev = &*mi;
    }
  }
  return changed;
}

bool RegisterRenamingIdem::renameRegsOverMI(MachineInstr *mi, MachineInstr* prev) {

  // update uses information of mi caused by prior change to instruction.
  regUses->computeRegUsesInfo(mi, prev);

  std::set<int> usesIn = regUses->UseIns[mi];
  if (usesIn.empty()) return false;
  // checks if the defined register was used by previous uses.
  unsigned reg = 0;
  int opIdx = -1;
  // finds defined register for mi.
  for (unsigned i = 0, e = mi->getNumOperands(); i < e; ++i) {
    auto op = mi->getOperand(i);
    if (op.isReg() && op.getReg() && op.isDef()) {
      reg = op.getReg();
      opIdx = i;
      break;
    }
  }
  if (!reg) return false;
  assert(TargetRegisterInfo::isPhysicalRegister(reg) && "should run this pass after RA!");
  assert(opIdx >= 0 && mi->getOperand(opIdx).isReg() && "must be register operand!");

  // checks if we should rename defined register of current mi.
  if (shouldRenameDefReg(reg, usesIn)) {
    const TargetRegisterClass *rc = tri->getMinimalPhysRegClass(reg);
    BitVector allocatable = tri->getAllocatableSet(*mf, rc);
    int newReg = 0;
    for (int idx = allocatable.find_first(); idx >= 0; idx = allocatable.find_next(idx)) {
      if (!usesIn.count(idx)) {
        newReg = idx;
        break;
      }
    }
    assert(newReg != 0 && "no free physical register for renaming");
    mi->getOperand(opIdx).setReg(newReg);
    old2NewRegsMap[reg] = newReg;

    // update subsequent instructions which uses the old reg.
    updateUsedRegBy(mi, reg, newReg);

    // update UseIn/UseOut dataflow information.
    regUses->addRegDef(mi, newReg);
    return true;
  }
  return false;
}

bool RegisterRenamingIdem::shouldRenameDefReg(int defReg,
                                              std::set<int> usesIn) {
  // verify each register contained in usesIn is physical register.
#ifdef NDEBUG
  for (int reg : usesIn)
    assert(TargetRegisterInfo::isPhysicalRegister(reg) && "should run this pass after RA!");
#endif

  return usesIn.find(defReg) != usesIn.end();
}
/**
 * Updates the oldReg used by instruction after mi with newReg.
 * @param mi
 * @param newReg
 */
void RegisterRenamingIdem::updateUsedRegBy(MachineInstr *mi,
                                           unsigned oldReg,
                                           unsigned newReg) {
  // We just visit those instructions after mi by iterate over seqs list.
  // Because reverse post-order ensure that def before use in seqs list.
  MachineBasicBlock *mbb = mi->getParent();
  assert(mbb && "mbb must not be null!");
  auto pos = std::find(seqs.begin(), seqs.end(), mbb);
  if (pos == seqs.end()) return;
  MachineBasicBlock::iterator itr(mi);
  ++itr;
  for (; itr != mbb->end(); ++itr)
    for (unsigned i = itr->getNumOperands(); i > 0; --i) {
      if (itr->getOperand(i-1).isReg() &&
          itr->getOperand(i-1).getReg() == oldReg) {
        if (itr->getOperand(i-1).isUse())
          itr->getOperand(i-1).setReg(newReg);
        else {
          // When a definition for this reg was seen, it indicates a redef, so
          // renaming over following instructions is not needed.
          return;
        }
      }
    }

  ++pos;
  while (pos != seqs.end()) {
    auto itr = (*pos)->begin();
    auto end = (*pos)->end();
    for (; itr !=end; ++itr) {
      for (unsigned i = itr->getNumOperands(); i > 0; --i) {
        if (itr->getOperand(i-1).isReg() &&
            itr->getOperand(i-1).getReg() == oldReg) {
            if (itr->getOperand(i-1).isUse())
              itr->getOperand(i-1).setReg(newReg);
            else {
              // When a definition for this reg was seen, it indicates a redef, so
              // renaming over following instructions is not needed.
              return;
            }
        }
      }
    }
    ++pos;
  }
}

FunctionPass *llvm::createRegisterRenamingPass() {
  return new RegisterRenamingIdem();
}