

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
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


static Constant* getLineNumber(LLVMContext& context, Instruction& inst)
{
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
	// First identify the functions we wish to track
	std::vector<llvm::Function*> toCount;
	for (auto& f : m)
	{
		toCount.push_back(&f);
	}

	populateInternals(toCount);

	// Create the CCOUNT(functionInfo) table used by the runtime library.
	// this table is simply an array of structures in the form {
	//		caller_func_name: string,
	//		caller_file_name: string,
	//		callsite_line: int32,
	//		callee_func_name: string,
	//		call_frequency: int64 (final value determined at runtime)
	//	}

	// Create the component types of the table
	auto* voidTy = Type::getVoidTy(context);
	auto* int32Ty = Type::getInt32Ty(context);
	auto* int64Ty = Type::getInt64Ty(context);
	auto* stringTy = Type::getInt8PtrTy(context);
	Type* fieldTys[] = {stringTy, stringTy, int32Ty, stringTy, int64Ty};
	auto* structTy = StructType::get(context, fieldTys, false);
	// initial value of call frequency
	auto* zero = ConstantInt::get(int64Ty, 0, false);

	// Declare the counter function
	auto* intSetterTy = FunctionType::get(voidTy, int64Ty, false);
	// call upon calling an external function
	auto* counter = m.getOrInsertFunction("CaLlPrOfIlEr_calling", intSetterTy);
	// call upon calling an internal function (pointer and direct)
	auto* callf = m.getOrInsertFunction("CaLlPrOfIlEr_funcPush", intSetterTy); // when peeking return pushed index
	// call upon calling an internal function (pointer and direct)
	auto* callfrange = m.getOrInsertFunction("CaLlPrOfIlEr_funcRangePush", intSetterTy); // exactly same as callf, except when peeking return input function index + pushed index
	// call upon entering an internal function
	auto* enter = m.getOrInsertFunction("CaLlPrOfIlEr_funcPop", intSetterTy); // return last pushed index on call stack

    // identify and record all function calls within modules into edges
    // to node is denoted by callee, and it is the name of the calling statement (unless it's a function pointer)
    //      in cases of function pointers, we have to rely on the function name provided at runtime
    //      in cases of external function calls, we have to rely on statement name at function call
    //      in all cases, each calling statement is given a unique value
    // from node is denoted by function name containing the function call
    // also ignore all llvm.dbg
	std::vector<Constant*> edges;
    Constant* filename = createConstantString(m, m.getName());
	// We only want to instrument internally implemented functions, so we take impls instead.
	for (auto funk : impls)
	{
		Constant* caller = createConstantString(m, funk->getName());
		for (auto& bb: *funk)
		{
        	for (auto& stmt : bb)
			{
				CallSite cs(&stmt);
				// Check whether the instruction is actually a function call
				if (!cs.getInstruction())
				{
					continue;
				}
				IRBuilder<> builder(cs.getInstruction());
				Constant* line = getLineNumber(context, stmt);
				if (!line)
				{
					throw debugInfoNotFound();
				}

				// called is a direct call
				auto directCall = dyn_cast<llvm::Function>(cs.getCalledValue()->stripPointerCasts());

				Constant* callee = nullptr;
				if (directCall)
				{
					StringRef callname = directCall->getName();
					// ignore if callee is llvm.dbg.*
					if (0 == std::string(callname.data()).compare(0, 9, "llvm.dbg."))
					{
						continue;
					}
					callee = createConstantString(m, callname);
				}

				if (callee)
				{
					// implemented functions are counted upon the entry of each function body.
					if (!impls.count(directCall))
					{
						// so only count external functions at their callsite
						builder.CreateCall(counter, builder.getInt64(edges.size()));
					}
					else
					{
						builder.CreateCall(callf, builder.getInt64(edges.size()));
					}
					Constant *structFields[] = {
						caller, filename, line, callee, zero
					};
					edges.push_back(ConstantStruct::get(structTy, structFields));
			    }
				else // call is a function pointer call
				{
					builder.CreateCall(callfrange, builder.getInt64(edges.size() + 1));

					// callee can be any internal implementation!
					for (llvm::Function* f : impls)
					{
						callee = createConstantString(m, f->getName());
						Constant *structFields[] = {
							caller, filename, line, callee, zero
						};
						edges.push_back(ConstantStruct::get(structTy, structFields));
					}
				}
			}
		}
	}

	for (auto funk : impls)
	{
		IRBuilder<> builder(&*funk->getEntryBlock().getFirstInsertionPt());
		builder.CreateCall(enter, builder.getInt64(ids[funk]));
	}

	auto* tableTy = ArrayType::get(structTy, edges.size());
	auto* functionTable = ConstantArray::get(tableTy, edges);
	new GlobalVariable(m,
        tableTy,
        false,
        GlobalValue::ExternalLinkage,
        functionTable,
        "CaLlPrOfIlEr_functionInfo");

	auto* numEdgesGlobal = ConstantInt::get(int64Ty, edges.size(), false);
	new GlobalVariable(m,
        int64Ty,
        true,
        GlobalValue::ExternalLinkage,
        numEdgesGlobal,
        "CaLlPrOfIlEr_numEdges");

	// inject the result printing function so that it prints out the counts after
	// the entire program is finished executing.
	auto* printer = m.getOrInsertFunction("CaLlPrOfIlEr_print", voidTy, nullptr);
	appendToGlobalDtors(m, cast<llvm::Function>(printer), 0);

	return true;
}


void ProfilingInstrumentationPass::populateInternals (ArrayRef<llvm::Function*> functions)
{
	size_t nextID = 0;
	for (auto f : functions)
	{
		if (!f->isDeclaration())
		{
			impls.insert(f);
			ids[f] = nextID;
			++nextID;
		}
	}
}

} // namespace cgprofiler
