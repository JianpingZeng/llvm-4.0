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
#include <llvm/CodeGen/Idempotence/RegisterUsesCollector.h>

using namespace llvm;

char RegisterUsesCollector::ID = 0;

/// Register this pass and initialize PassInfo.
char &llvm::RegisterUseCollectorID = RegisterUsesCollector::ID;
INITIALIZE_PASS(RegisterUsesCollector, "reguse-collector",
                "Register Use Collector", false, false)

bool RegisterUsesCollector::runOnMachineFunction(llvm::MachineFunction &MF) {
  MachineBasicBlock* entry = &MF.front();

  std::vector<MachineBasicBlock*> res;
  // iterate the machine CFG in the order of post order.
  reversePostOrder(entry, res);

  // traverse machine block.
  for (MachineBasicBlock *mbb : res) {
    auto mi = mbb->begin();
    auto end = mbb->end();

    // 	add	r0, r0, r1  (def r0, use r0, r1)
    //	add	r0, r0, #1  ()
    //	mov	pc, lr

    // Dataflow equation as follows.
    // UseIn = U(UseOut_pred) U localUses
    // UseOut = UseIn - localDefs.
    SmallBitVector localUses, localDefs;
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
      SmallBitVector &useIn = UseIns[&*mi];
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
      SmallBitVector &useOut = UseOuts[&*mi];
      intersect(useOut, useIn, localDefs);
    }

    ++mi;
    for (; mi != end; ++mi) {

      // UseIn = U(UseOut_pred) U localUses
      // UseOut = UseIn - localDefs.
      SmallBitVector &useIn = UseOuts[&*mi];
      Union(useIn, UseOuts[(&*mi) - 1], localUses);
      intersect(UseOuts[&*mi], useIn, localDefs);
    }
  }
  return false;
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
  if (!entry || visited.count(entry)) return;
  visited.insert(entry);
  if (entry->succ_empty()) return;

  MachineBasicBlock::succ_iterator itr = entry->succ_begin();
  MachineBasicBlock::succ_iterator end = entry->succ_end();
  while (itr != end) {
    traverse(*itr, res, visited);
    ++itr;
  }
  res.push_back(entry);
}

void RegisterUsesCollector::computeLocalDefUses(MachineInstr* mi,
                         SmallBitVector& defs,
                         SmallBitVector &uses) {
  unsigned i = 0, size = mi->getNumOperands();
  for (; i < size; ++i) {
    MachineOperand &op = mi->getOperand(i);
    if (op.isReg() && op.getReg() != 0) {
      if (op.isDef())
        defs.set(op.getReg());
      else if (op.isUse())
        uses.set(op.getReg());
    }
  }
}
/**
 * performs intersect operation over lhs and rhs. Stores result
 * into lhs.
 * @param lhs
 * @param rhs
 */
void RegisterUsesCollector::intersect(SmallBitVector &res,
                                     SmallBitVector &lhs,
                                     SmallBitVector &rhs) {
  for (int start = lhs.find_first();
       start >= 0;
       start = lhs.find_next(start)) {

    if (!rhs[start])
      res[start] = true;
  }
}

void RegisterUsesCollector::Union(SmallBitVector &res,
                                  SmallBitVector &lhs,
                                  SmallBitVector &rhs) {
  for (int start = rhs.find_first();
       start >= 0;
       start = rhs.find_next(start)) {

    res[start] = true;
  }
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
  if (UseIns[mi].empty())
    return false;
  SmallBitVector &useIns = UseIns[mi];
  for(int idx = useIns.find_first(); idx >= 0; idx = useIns.find_next(idx)) {
    int reg = tri->isPhysicalRegister(idx) ? idx : vrm->getPhys(idx);
    if (reg == phyReg) return true;
  }
  return false;
}
