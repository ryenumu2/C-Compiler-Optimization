/* * * * * * * * * * * * * * * * * * * *\
|          CSE and Optimization         |
\* * * * * * * * * * * * * * * * * * * */

/*
 * File: CSE.cpp
 *
 * Description:
 *   This file will visit each instruction, check if it's dead or
 *   can be simplified via constant folding, and use common
 *   subexpression elimination to eliminate instructions with the same opcode,
 *   type, or number of operands.
 */

/* LLVM Header Files */
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Type.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/IR/InstIterator.h"
#include <iostream>
#include <map>
#include "dominance.h"

using namespace llvm;
#include "CSE.h"

/* Need these includes to support the LLVM 'cast' template for the C++ 'wrap'
   and 'unwrap' conversion functions. */
#include "llvm/IR/Module.h"
#include "llvm/PassRegistry.h"
#include "llvm/IR/IRBuilder.h"


/* Need these includes to support the LLVM 'cast' template for the C++ 'wrap'
   and 'unwrap' conversion functions. */
#include "llvm/IR/Module.h"
#include "llvm/PassRegistry.h"
#include "llvm/IR/IRBuilder.h"

int CSEDead=0;
int CSEElim=0;
int CSESimplify=0;
int CSELdElim=0;
int CSELdStElim=0;
int CSERStElim=0;

void LLVMCommonSubexpressionElimination_Cpp(Module *M)
{
  // for each function, f:
  //   FunctionCSE(f);
  RunDeadCodeElimination(*M);
  RunConstantFolding(*M);
  RunCommonSubExpressionElimination(*M);

  // print out summary of results
  fprintf(stderr,"CSE_Dead.....%d\n", CSEDead);
  fprintf(stderr,"CSE_Basic.....%d\n", CSEElim);
  fprintf(stderr,"CSE_Simplify..%d\n", CSESimplify);
  fprintf(stderr,"CSE_RLd.......%d\n", CSELdElim);
  fprintf(stderr,"CSE_RSt.......%d\n", CSERStElim);
  fprintf(stderr,"CSE_LdSt......%d\n", CSELdStElim);
}


bool _AreInstructionsLiteralMatches(llvm::Instruction  &i, llvm::Instruction  &j) {
  //check if i is not needed to simplify
  if (isa<VAArgInst>(i) ||
      isa<FCmpInst>(i) ||
      isa<AllocaInst>(i) ||
      isa<CallInst>(i) ||
      isa<LoadInst>(i) ||
      isa<StoreInst>(i) ||
      isa<BranchInst>(i) ||
      i.isTerminator()) {
        return false;
  }
  return i.isIdenticalTo(&j);
}

void _ReplaceAllUsesInBlock(BasicBlock::iterator &startingInstruction, BasicBlock::iterator &end, llvm::Instruction  &i) {
  while(startingInstruction != end) {
    if (_AreInstructionsLiteralMatches(i, *startingInstruction)) {
      llvm::Instruction *rm = &(*startingInstruction);
      startingInstruction++;
      rm->replaceAllUsesWith(&i);
      rm->eraseFromParent();
      CSEElim++;
    } else {
      startingInstruction++;
    }
  }
}

void RunCommonSubExpressionElimination(Module &M) {
   for (Module::iterator f = M.begin(); f != M.end(); f++) {
    for (Function::iterator bb=(*f).begin(); bb!=(*f).end(); bb++) {
      for (BasicBlock::iterator inst_iter = (*bb).begin(); inst_iter != (*bb).end(); inst_iter++) {
          // iterate through all the instructions that dominate inst_iter
          BasicBlock::iterator next_iter = inst_iter;
          next_iter++;
          BasicBlock::iterator currEnd = (*bb).end();
          llvm::BasicBlock *currBlock = (*inst_iter).getParent();
          while (currBlock != nullptr) {
            currEnd = currBlock->end();
            _ReplaceAllUsesInBlock(next_iter, currEnd, *inst_iter);
            currBlock = unwrap(LLVMNextDomChild(wrap(&*bb), wrap(currBlock)));
          }
        }
      }
  }
}

void RunDeadCodeElimination(Module &M) {
  std::set<Instruction*> worklist;

  for (Module::iterator f = M.begin(); f != M.end(); f++) {
    for (Function::iterator bb=(*f).begin(); bb!=(*f).end(); bb++) {
      for (BasicBlock::iterator I = (*bb).begin(); I != (*bb).end(); I++) {
        if (_isDead(*I)) {
          CSEDead++;
        }
      }
    }
  }
}

bool _isDead(Instruction &I) {
  int opcode = I.getOpcode();
  switch(opcode){
  case Instruction::Add:
  case Instruction::FNeg:
  case Instruction::FAdd:
  case Instruction::Sub:
  case Instruction::FSub:
  case Instruction::Mul:
  case Instruction::FMul:
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::FDiv:
  case Instruction::URem:
  case Instruction::SRem:
  case Instruction::FRem:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Alloca:
  case Instruction::GetElementPtr:
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
  case Instruction::ICmp:
  case Instruction::FCmp:
  case Instruction::PHI:
  case Instruction::Select:
  case Instruction::ExtractElement:
  case Instruction::InsertElement:
  case Instruction::ShuffleVector:
  case Instruction::ExtractValue:
  case Instruction::InsertValue:
    if ( I.use_begin() == I.use_end() )
      {
	      return true;
      }
    break;
  case Instruction::Load:
    {
      LoadInst *li = dyn_cast<LoadInst>(&I);
      if (li->isVolatile()) { return false; }
      if ( I.use_begin() == I.use_end() )
      {
        return true;
      }
      break;
    }
  default: // any other opcode fails (includes stores and branches)
    // we don't know about this case, so conservatively fail!
    return false;
  }

  return false;
}

bool isFoldable(Instruction &I)
{
  int opcode = I.getOpcode(); //returns a member of the enums: unsigned llvm::Instruction::getOpcode ( ) const
  switch(opcode)
  {       //all integer types can be folded aggressively
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::SDiv:
    if (I.getOperand(1)==0) //if divide by 0, return false, is not foldable
    {
      return false;
    }
  case Instruction::UDiv:
    if (I.getOperand(1)==0) //if divide by 0, return false, is not foldable
    {
      return false;
    }
    return true;
  default:
    return false;

  }
    return false;
}


void RunConstantFolding(Module &M)  //goal: compiler should compute constants on its own
{
  for(auto f = M.begin(); f!=M.end(); f++)       // loop over functions, where auto is Module:: iterator
    {
      for(auto bb= f->begin(); bb!=f->end(); bb++)  // loop over functions, where auto is Function:: iterator
         {
            // loop over basic blocks
           for(auto i = bb->begin(); i != bb->end(); i++) // loop over functions, where auto is BasicBlock:: iterator
               {
                 if (isFoldable(*i))
                  {
                     LLVMValueRef InstructionSimplify(LLVMValueRef I);
                     CSESimplify++;
                  }
              }
          }
    }
}
