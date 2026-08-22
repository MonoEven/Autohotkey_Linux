// Parity classification for the Linux port (check_detail0821 §13).
// The classification table lives in parity_data.h (AUTO-GENERATED from
// tests/doccheck/parity.tsv by tools/gen_parity.py).  Levels:
//   P1 compatible (default) / P2 adapted / P3 simulated / P4 unavailable.
#include "../parity_data.h"
#include "../../stdafx.h"
#include <cstring>
#include <cstdio>

// Look up a function's parity level (1..4, P1 default) and note.
// Returns the note (or nullptr when the function is not in the table / P1).
extern "C" const char *LinuxParityLookup(const char *aName, int &aLevel)
{
	aLevel = 1;
	if (!aName || !*aName)
		return nullptr;
	for (int i = 0; i < kLinuxParityCount; ++i)
		if (!strcasecmp(kLinuxParity[i].name, aName))
		{
			aLevel = kLinuxParity[i].level;
			return kLinuxParity[i].note;
		}
	return nullptr;
}

// ahk_core --parity <FuncName>: print the level + note and exit.
extern "C" int LinuxRunParity(const char *aName)
{
	int level = 1;
	const char *note = LinuxParityLookup(aName ? aName : "", level);
	const char *label = level == 1 ? "compatible"
		: level == 2 ? "adapted"
		: level == 3 ? "simulated"
		: "unavailable";
	std::printf("parity %s : P%d %s%s%s\n",
		aName ? aName : "",
		level, label,
		note ? " -- " : "",
		note ? note : "");
	return 0;
}
