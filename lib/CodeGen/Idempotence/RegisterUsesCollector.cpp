//===-- UseInfoCollector.cpp - Collect Register use information -----------===//
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

#define  DEBUG_TYPE "idem"
#include <llvm/CodeGen/Passes.h>
#include <llvm/PassSupport.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/ADT/SmallBitVector.h>
#include <set>
#include <map>
#include <vector>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetInstrInfo.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/CodeGen/Idempotence/RegisterUsesCollector.h>

using namespace llvm;

char RegisterUsesCollector::ID = 0;

/// Register this pass and initialize PassInfo.
char &llvm::RegisterUseCollectorID = RegisterUsesCollector::ID;
INITIALIZE_PASS(RegisterUsesCollector, "reguse-collector",
                "Register Use Collector", false, false)

bool RegisterUsesCollector::runOnMachineFunction(llvm::MachineFunction &MF) {
  MachineBasicBlock* entry = &MF.front();
  tri = MF.getSubtarget().getRegisterInfo();
  tii = MF.getSubtarget().getInstrInfo();

  // iterate the machine CFG in the order of post order.
  reversePostOrder(entry, reverseOrderSequences);

  // traverse machine block.
  for (MachineBasicBlock *mbb : reverseOrderSequences) {
    auto mi = mbb->begin();
    auto end = mbb->end();

    // 	add	r0, r0, r1  (def r0, use r0, r1)
    //	add	r0, r0, #1  ()
    //	mov	pc, lr

    // Dataflow equation as follows.
    // UseIn = U(UseOut_pred) U localUses
    // UseOut = UseIn - localDefs.
    std::set<int> localUses, localDefs;
    computeLocalDefUses(&*mi, localDefs, localUses);

    // Specially handle first instr.
    if (mbb->pred_empty()) {
      // UseIn = localUses
      // UseOut = UseIn - localDefs.
      UseIns[&*mi] = localUses;
      intersect(UseOuts[&*mi], localUses, localDefs);
    }
    else {
      // set up UseIn for current machine instr.
      std::set<int> &useIn = UseIns[&*mi];
      auto pred = mbb->pred_begin();
      auto end = mbb->pred_end();
      MachineInstr* last = &*(*pred)->getLastNonDebugInstr();
      Union(useIn, useIn, UseOuts[last]);
      ++pred;
      for (; pred != end; ++pred) {
        last = &*(*pred)->getLastNonDebugInstr();
        Union(useIn, useIn, UseOuts[last]);
      }
      Union(useIn, useIn, localUses);

      // setup UseOut for mi.
      std::set<int> &useOut = UseOuts[&*mi];
      intersect(useOut, useIn, localDefs);
    }

    auto prev = mi;
    ++mi;
    for (; mi != end; ++mi) {
      localDefs.clear();
      localUses.clear();
      computeLocalDefUses(&*mi, localDefs, localUses);
      // UseIn = U(UseOut_pred) U localUses
      // UseOut = UseIn - localDefs.
      std::set<int> &useIn = UseIns[&*mi];
      Union(useIn, UseOuts[&*prev], localUses);
      intersect(UseOuts[&*mi], useIn, localDefs);
      prev = mi;
    }
  }
  // prints generated register uses information for debugging.
  dump();
  return false;
}

void RegisterUsesCollector::dump() {
  // prints generated register uses information for debugging.
  for (auto mbb : reverseOrderSequences) {
    auto mi = mbb->begin();
    auto end = mbb->end();
    for (; mi != end; ++mi) {
      if (&*mi == nullptr)
        continue;

      // dump machine instr information.
      llvm::errs() << "MI:   ";
      mi->dump(tii);
      llvm::errs() << "UseIns: [";
      printSet(&*mi, UseIns[&*mi]);
      llvm::errs() << "UseOuts:[";
      printSet(&*mi, UseOuts[&*mi]);
      llvm::errs()<<"\n";
    }
  }
}

void RegisterUsesCollector::printSet(MachineInstr*mi,
                                     std::set<int> &bits) {
  bool firstReg = true;
  for (int reg : bits) {
    if (firstReg)
      firstReg = false;
    else
      llvm::errs() << ", ";
    assert(reg != 0 && "register should be 0!");
    if (tri && tri->isPhysicalRegister(reg))
      llvm::errs() << tri->getName(reg);
    else
      llvm::errs() << "%vreg"
                   << TargetRegisterInfo::virtReg2Index(reg);
  }
  llvm::errs() << "]\n";
}

void RegisterUsesCollector::reversePostOrder(MachineBasicBlock* entry,
                      std::vector<MachineBasicBlock*> &res) {
  std::set<MachineBasicBlock*> visited;
  traverse(entry, res, visited);
  std::reverse(res.begin(), res.end());
}
void RegisterUsesCollector::traverse(MachineBasicBlock* entry,
              std::vector<MachineBasicBlock*> &res,
              std::set<MachineBasicBlock*> &visited) {
  if (!entry || visited.count(entry))
    return;

  visited.insert(entry);
  if (!entry->succ_empty()){
    MachineBasicBlock::succ_iterator itr = entry->succ_begin();
    MachineBasicBlock::succ_iterator end = entry->succ_end();
    while (itr != end) {
      traverse(*itr, res, visited);
      ++itr;
    }
  }
  res.push_back(entry);
}

void RegisterUsesCollector::computeLocalDefUses(MachineInstr* mi,
                         std::set<int>& defs,
                         std::set<int> &uses) {
  unsigned i = 0, size = mi->getNumOperands();
  for (; i < size; ++i) {
    MachineOperand &op = mi->getOperand(i);
    if (!op.isReg() || !op.getReg()) continue;
    int reg = op.getReg();
    if (op.isDef())
      defs.insert(reg);
    else if (op.isUse())
      uses.insert(reg);
  }
}
/**
 * performs intersect operation over lhs and rhs. Stores result
 * into lhs.
 * @param lhs
 * @param rhs
 */
void RegisterUsesCollector::intersect(std::set<int> &res,
                                     std::set<int> &lhs,
                                     std::set<int> &rhs) {
  for (int reg : lhs)
    if (!rhs.count(reg))
      res.insert(reg);
}

void RegisterUsesCollector::Union(std::set<int> &res,
                                  std::set<int> &lhs,
                                  std::set<int> &rhs) {
  res.insert(lhs.begin(), lhs.end());
  res.insert(rhs.begin(), rhs.end());
}
/**
 * Checks if phyReg is used by previous physical register assigned to
 * previous used virtual register.
 * @param mi
 * @param phyReg
 * @param vrm
 * @param tri
 * @return
 */
bool RegisterUsesCollector::isPhyRegUsedBeforeMI(MachineInstr *mi,
                                                 int phyReg,
                                                 VirtRegMap *vrm,
                                                 const TargetRegisterInfo *tri) {
  assert(TargetRegisterInfo::isPhysicalRegister(phyReg) && "must be physical register");

  if (UseIns[mi].empty())
    return false;
  std::set<int> &useIns = UseIns[mi];
  for(int idx : useIns) {
    int reg = TargetRegisterInfo::isVirtualRegister(idx) ? vrm->getPhys(idx) : idx;
    if (reg == phyReg) return true;
  }
  return false;
}
