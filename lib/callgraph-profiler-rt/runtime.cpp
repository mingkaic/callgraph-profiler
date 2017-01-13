
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>

extern "C" {


// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. CGPROF(entry) yields CaLlPrOfIlEr_entry
#define CGPROF(X) CaLlPrOfIlEr_ ## X

// shows up as global integer `CaLlPrOfIlEr_numFunctions`
extern uint64_t CGPROF(numFunctions);

// shows up as struct `CaLlPrOfIlEr_functionInfo` with space (char*, uint64_t)
extern struct {
	char* caller;
	char* callmodule;
	uint64_t line;
	char* callee;
	uint64_t count;
} CGPROF(functionInfo)[];


// shows up as method `CaLlPrOfIlEr_called`
void CGPROF(called)(uint64_t id) {
	++CGPROF(functionInfo)[id].count;
}

// shows up as method `CaLlPrOfIlEr_print`
void CGPROF(print)() {
	// stream to output file: profile-results.csv
	std::ofstream results ("profile-results.csv", std::ofstream::out);

	// for all functions record its info
	for (size_t id = 0; id < CGPROF(numFunctions); ++id) {
		auto& info = CGPROF(functionInfo)[id];
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
