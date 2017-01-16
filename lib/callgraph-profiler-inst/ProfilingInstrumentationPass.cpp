#include <iostream>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "ProfilingInstrumentationPass.h"

using namespace llvm;
using namespace cgprofiler;


namespace cgprofiler
{

char ProfilingInstrumentationPass::ID = 0;


struct debugInfoNotFound : std::exception
{
	const char* what() const noexcept
	{
		return "input file does not contain debug info necessary to analysize line info";
	}
};


enum CALLCASE
{
	EXTERNAL = 0,
	DIRECT,
	FUNCPTR
};


// helper function for creating a constant string accessible at runtime
static Constant* createConstantString(Module& m, StringRef str)
{
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


static Constant* getFilename(Module& m, Instruction& inst)
{
	const DebugLoc& loc = inst.getDebugLoc();
	StringRef fname = m.getName();
	if (loc) {
		fname = loc->getFilename();
	}
	return createConstantString(m, fname);
}


static Constant* getLineNumber(Module& m, Instruction& inst)
{
	auto& context = m.getContext();
	const DebugLoc& loc = inst.getDebugLoc();
	if (loc) {
		unsigned line = loc.getLine(); // line numbers start from 1
		return ConstantInt::get(Type::getInt32Ty(context), line);
	}
	return nullptr;
}


bool ProfilingInstrumentationPass::runOnModule(Module& m)
{
	auto& context = m.getContext();
	initInternals(m);

	// Create the component types of the table
	auto* voidTy = Type::getVoidTy(context);
	auto* int64Ty = Type::getInt64Ty(context);
	auto* stringTy = Type::getInt8PtrTy(context);
	Type* fieldTys[] = {stringTy, stringTy, Type::getInt32Ty(context), stringTy, int64Ty};
	auto* structTy = StructType::get(context, fieldTys, false);

	auto* intSetterTy = FunctionType::get(voidTy, int64Ty, false);
	// increment CaLlPrOfIlEr_edgeInfo[input] frequency
	auto externalCall = m.getOrInsertFunction("CaLlPrOfIlEr_calling", intSetterTy);
	// push edgeInfo index into runtime call stack
	auto directCall = m.getOrInsertFunction("CaLlPrOfIlEr_funcPush", intSetterTy);
	auto ptrCall = m.getOrInsertFunction("CaLlPrOfIlEr_funcRangePush", intSetterTy);

	// Global variables
	auto* tableTy = ArrayType::get(structTy, edges.size());
	auto* functionTable = ConstantArray::get(tableTy, edges);
	new GlobalVariable(m,
        tableTy, false,
        GlobalValue::ExternalLinkage,
        functionTable, "CaLlPrOfIlEr_edgeInfo");

	auto* numEdgesGlobal = ConstantInt::get(int64Ty, edges.size(), false);
	new GlobalVariable(m,
        int64Ty, true,
        GlobalValue::ExternalLinkage,
        numEdgesGlobal, "CaLlPrOfIlEr_numEdges");

	// initial value of call frequency
	auto* zero = ConstantInt::get(int64Ty, 0, false);

    // identify and record all function calls within modules into edges
    // to node is denoted by callee, and it is the name of the calling statement (unless it's a function pointer)
    //      in cases of function pointers, we have to rely on the function name provided at runtime
    //      in cases of external function calls, we have to rely on statement name at function call
    //      in all cases, each calling statement is given a unique value
    // from node is denoted by function name containing the function call
    // also ignore all llvm.dbg
	std::vector<Constant*> edges;
	// We only want to instrument internally implemented functions, so we take impls instead.
	for (auto f_imps : impls)
	{
		llvm::Function* funk = f_imps.first;
		Constant* caller = createConstantString(m, funk->getName());
		for (auto& bb: *funk)
		{
        	for (auto& stmt : bb)
			{
				std::vector<StringRef> callees = handleCallees(CallSite(&stmt),
					[&edges, externalCall, directCall, ptrCall]
					(IRBuilder<>& builder, size_t callcase)
				{
					size_t currentIdx = edges.size();
					switch(callcase)
					{
						case EXTERNAL:
							builder.CreateCall(externalCall, builder.getInt64(currentIdx));
							break;
						case DIRECT:
							builder.CreateCall(directCall, builder.getInt64(currentIdx));
							break;
						case FUNCPTR:
							builder.CreateCall(ptrCall, builder.getInt64(currentIdx));
							break;
					}
				});
				// callee is empty is stmt isn't a call or an edge we should record
				if (!callees.empty())
				{
					Constant* filename = getFilename(m, stmt);
					Constant* line = getLineNumber(m, stmt);
					if (!line)
					{
						throw debugInfoNotFound();
					}
					for (StringRef callname : callees)
					{
						Constant* callee = createConstantString(m, callname);
						Constant *structFields[] = {
							caller, filename, line, callee, zero
						};
						edges.push_back(ConstantStruct::get(structTy, structFields));
					}
				}
			}
		}
	}

	auto popCallStack = m.getOrInsertFunction("CaLlPrOfIlEr_funcPop",
		FunctionType::get(voidTy, int64Ty, false));
	for (auto f_imps : impls)
	{
		llvm::Function* funk = f_imps.first;
		IRBuilder<> builder(&*funk->getEntryBlock().getFirstInsertionPt());
		// pop edgeInfo index from call stack, adding id to index iff the edgeInfo index is pushed from callfrange
		builder.CreateCall(popCallStack, builder.getInt64(impls[funk]));
	}

	// inject the result printing function so that it prints out the counts after
	// the entire program is finished executing.
	auto* printer = m.getOrInsertFunction("CaLlPrOfIlEr_print", voidTy, nullptr);
	appendToGlobalDtors(m, cast<llvm::Function>(printer), 0);

	return true;
}


std::vector<StringRef> ProfilingInstrumentationPass::handleCallees (CallSite cs,
	std::function<void(IRBuilder<>&, size_t)> injectCall)
{
	// Check whether the instruction is actually a function call
	if (!cs.getInstruction())
	{
		return std::vector<StringRef>();
	}

	IRBuilder<> builder(cs.getInstruction());
	std::vector<StringRef> callees;
	auto directCall = dyn_cast<llvm::Function>(cs.getCalledValue()->stripPointerCasts());
	// for all injected function calls, argument is the starting index of the edge info being inserted
	if (directCall)
	{
		// store callee name
		StringRef callname = directCall->getName();
		// ignore llvm.dbg.*
		if (0 == std::string(callname.data()).compare(0, 9, "llvm.dbg."))
			return std::vector<StringRef>();

		callees.push_back(callname);

		// inject function calls
		if (!impls.count(directCall)) // external function calls
		{
			injectCall(builder, EXTERNAL);
		}
		else // internal function calls
		{
			injectCall(builder, DIRECT);
		}
	}
	else // call is a function pointer call
	{
		// callees must be internally implemented functions ordered by id, so pad with nulls
		// then inject info in the correct id order
		callees.insert(callees.end(), impls.size(), StringRef());
		for (auto imp : impls)
		{
			llvm::Function* f = imp.first;
			uint64_t id = imp.second;
			callees[id] = f->getName();
		}
		// inject function calls
		injectCall(builder, FUNCPTR);
	}
	return callees;
}


void ProfilingInstrumentationPass::initInternals (Module& m)
{
	size_t nextID = 0;
	for (auto& f : m)
	{
		if (!f.isDeclaration())
		{
			impls[&f] = nextID;
			++nextID;
		}
	}
}

} // namespace cgprofiler
