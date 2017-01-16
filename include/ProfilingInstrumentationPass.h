

#ifndef PROFILING_INSTRUMENTATION_PASS_H
#define PROFILING_INSTRUMENTATION_PASS_H


#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
// temp
#include "llvm/Support/raw_ostream.h"

namespace cgprofiler {


struct ProfilingInstrumentationPass : public llvm::ModulePass {
	static char ID;

	// uniquely and dynamically enumerate internally implemented functions
	llvm::DenseMap<llvm::Function*, uint64_t> impls;

	ProfilingInstrumentationPass()
	: llvm::ModulePass(ID) {}

	bool runOnModule(llvm::Module& m) override; // instrumentation pass entrance

private:
	void initInternals (llvm::Module& m);

	std::vector<llvm::StringRef> handleCallees (llvm::CallSite cs,
		std::function<void(llvm::IRBuilder<>&, size_t)> injectCall);
};


}


#endif
