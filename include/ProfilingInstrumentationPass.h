

#ifndef PROFILING_INSTRUMENTATION_PASS_H
#define PROFILING_INSTRUMENTATION_PASS_H


#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
// temp
#include "llvm/Support/raw_ostream.h"

namespace cgprofiler {


struct ProfilingInstrumentationPass : public llvm::ModulePass {
	static char ID;

	// uniquely and dynamically enumerate internally implemented functions
	llvm::DenseMap<llvm::Function*, uint64_t> ids;
	// set of internally implemented functions
	llvm::DenseSet<llvm::Function*> impls;

	ProfilingInstrumentationPass()
	: llvm::ModulePass(ID) {}

	bool runOnModule(llvm::Module& m) override; // instrumentation pass entrance

private:
	void createEdgeTable(llvm::Module& m);

	void populateInternals (llvm::ArrayRef<llvm::Function*> functions);
};


}


#endif
