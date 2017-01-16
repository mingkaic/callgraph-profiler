
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stack>

#if HAS_CXX11_THREAD_LOCAL
    #define TL_REINFORCE thread_local
#endif

extern "C" {


// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. CGPROF(entry) yields CaLlPrOfIlEr_entry
#define CGPROF(X) CaLlPrOfIlEr_ ## X
// shows up as global integer `CaLlPrOfIlEr_numEdges`
extern uint64_t CGPROF(numEdges);

// shows up as struct array `CaLlPrOfIlEr_edgeInfo` with space (char*, uint64_t)
extern struct {
	char* caller;
	char* callmodule;
	uint32_t line;
	char* callee;
	uint64_t count;
} CGPROF(edgeInfo)[];


// shows up as method `CaLlPrOfIlEr_calling`
void CGPROF(calling)(uint64_t id) {
	if (id < CaLlPrOfIlEr_numEdges) {
		++CGPROF(edgeInfo)[id].count;
	}
}


// internal stack
struct inFunc
{
	uint64_t inId;
	bool takeFunc;
	inFunc(uint64_t id, bool take) : inId(id), takeFunc(take) {}
};


#ifdef TLS_REINFORCE
	static TL_REINFORCE std::stack<inFunc> inEdge;
#else
	static std::stack<inFunc> inEdge;
#endif


// call stack push/peek approach (HIGHLY COUPLED)
void CGPROF(funcPush)(uint64_t id) {
	inEdge.push(inFunc(id, false));
}


void CGPROF(funcRangePush)(uint64_t id) {
	inEdge.push(inFunc(id, true));
}


void CGPROF(funcPop)(uint64_t func_id) {
	if (inEdge.empty())
	{
		// we've just started the program. no other internal nodes visited
		return;
	}
	inFunc& ifunc = inEdge.top();
	uint64_t idx = ifunc.inId;
	if (ifunc.takeFunc)
	{
		idx += func_id;
	}
	CGPROF(calling)(idx);
	inEdge.pop();
}
// end call stack approach


// shows up as method `CaLlPrOfIlEr_print`
void CGPROF(print)() {
	// stream to output file: profile-results.csv
	std::ofstream results ("profile-results.csv", std::ofstream::out);

	// for all functions record its info
	for (size_t id = 0; id < CGPROF(numEdges); ++id) {
		auto& info = CGPROF(edgeInfo)[id];
		if (info.count > 0) {
			// format is <caller name>, <callsite filename>, <call site line #>, <callee name>, <frequency>
			results << info.caller << ", "
				<< info.callmodule << ", "
				<< info.line << ", "
				<< info.callee << ", "
				<< info.count << "\n";
		}
	}
	results.close();
}

}
