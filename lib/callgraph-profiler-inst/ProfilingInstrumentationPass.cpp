

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "ProfilingInstrumentationPass.h"

using namespace llvm;
using namespace cgprofiler;


namespace cgprofiler {

char ProfilingInstrumentationPass::ID = 0;

} // namespace cgprofiler


static Constant* createConstantString(Module& m, StringRef str) {
	auto& context = m.getContext();

	auto* name = ConstantDataArray::getString(context, str, true);
	auto* int8Ty = Type::getInt8Ty(context);

	auto* arrayTy = ArrayType::get(int8Ty, str.size() + 1);
	auto* asStr = new GlobalVariable(
			m, arrayTy, true, GlobalValue::PrivateLinkage, name);

	auto* zero = ConstantInt::get(Type::getInt32Ty(context), 0);
	Value* indices[] = {zero, zero};
	return ConstantExpr::getInBoundsGetElementPtr(arrayTy, asStr, indices);
}


// Create the CCOUNT(functionInfo) table used by the runtime library.
static void createFunctionTable(Module& m, uint64_t numFunctions) {
	auto& context = m.getContext();

	// Create the component types of the table
	auto* int64Ty = Type::getInt64Ty(context);
	auto* stringTy = Type::getInt8PtrTy(context);
	Type* fieldTys[] = {stringTy, stringTy, int64Ty, stringTy, int64Ty};
	auto* structTy = StructType::get(context, fieldTys, false);
	auto* tableTy = ArrayType::get(structTy, numFunctions);
	auto* zero = ConstantInt::get(int64Ty, 0, false);

    const std::string filename = m.getModuleIdentifier();

    // m is iterated over as an array of functions
	// Compute and store an externally visible array of function information.
	std::vector<Constant*> values;
	std::transform(
			m.begin(),
			m.end(),
			std::back_inserter(values),
			[&m, zero, structTy](auto& f) {
                // arguments <caller name>, <callsite filename>, <call site line #>, <callee name>, <frequency>
                // TODO: find callsite line number
                // TODO: find callsite file name
                // TODO: find caller name
                Constant* callee = createConstantString(m, f.getName());
				Constant* structFields[] = {
                    callee, callee, zero, callee, zero
                };
				return ConstantStruct::get(structTy, structFields);
			});
	auto* functionTable = ConstantArray::get(tableTy, values);
	new GlobalVariable(m,
        tableTy,
        false,
        GlobalValue::ExternalLinkage,
        functionTable,
        "CaLlPrOfIlEr_functionInfo");
}


bool ProfilingInstrumentationPass::runOnModule(Module& m) {auto& context = m.getContext();

	// First identify the functions we wish to track
	std::vector<llvm::Function*> toCount;
	for (auto& f : m) {
		toCount.push_back(&f);
	}

	populateIds(toCount);
	populateInternals(toCount);
	auto const numFunctions = toCount.size();

	// Store the number of functions into an externally visible variable.
	auto* int64Ty = Type::getInt64Ty(context);
	auto* numFunctionsGlobal = ConstantInt::get(int64Ty, numFunctions, false);
	new GlobalVariable(m,
        int64Ty,
        true,
        GlobalValue::ExternalLinkage,
        numFunctionsGlobal,
        "CaLlPrOfIlEr_numFunctions");

	createFunctionTable(m, numFunctions);

	// Install the result printing function so that it prints out the counts after
	// the entire program is finished executing.
	auto* voidTy = Type::getVoidTy(context);
	auto* printer = m.getOrInsertFunction("CaLlPrOfIlEr_print", voidTy, nullptr);
	appendToGlobalDtors(m, cast<llvm::Function>(printer), 0);

	// Declare the counter function
	auto* helperTy = FunctionType::get(voidTy, int64Ty, false);
	auto* counter = m.getOrInsertFunction("CaLlPrOfIlEr_called", helperTy);

	for (auto f : toCount) {
		// We only want to instrument internally defined functions.
		if (f->isDeclaration()) {
			continue;
		}

		// Count each internal function as it executes.
		handleCalledFunction(*f, counter);

		// Count each external function as it is called.
		for (auto& bb : *f) {
			for (auto& i : bb) {
				handleInstruction(CallSite(&i), counter);
			}
		}
	}

	return true;
}


void ProfilingInstrumentationPass::handleCalledFunction(llvm::Function& f, Value* counter) {
	IRBuilder<> builder(&*f.getEntryBlock().getFirstInsertionPt());
	builder.CreateCall(counter, builder.getInt64(ids[&f]));
}


void ProfilingInstrumentationPass::handleInstruction(CallSite cs, Value* counter) {
	// Check whether the instruction is actually a call
	if (!cs.getInstruction()) {
		return;
	}

	// Check whether the called function is directly invoked
	auto called = dyn_cast<llvm::Function>(cs.getCalledValue()->stripPointerCasts());
	if (!called) {
		return;
	}

	// Check if the function is internal or blacklisted.
	if (internal.count(called) || !ids.count(called)) {
		// Internal functions are counted upon the entry of each function body.
		// Blacklisted functions are not counted. Neither should proceed.
		return;
	}

	// External functions are counted at their invocation sites.
	IRBuilder<> builder(cs.getInstruction());
	builder.CreateCall(counter, builder.getInt64(ids[called]));
}


void ProfilingInstrumentationPass::populateIds (ArrayRef<llvm::Function*> functions) {
	size_t nextID = 0;
	for (auto f : functions) {
		ids[f] = nextID;
		++nextID;
	}
}


void ProfilingInstrumentationPass::populateInternals (ArrayRef<llvm::Function*> functions) {
	for (auto f : functions) {
		if (!f->isDeclaration()) {
			internal.insert(f);
		}
	}
}
