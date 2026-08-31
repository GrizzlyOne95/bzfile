#include "bzfile_version.h"

// Native consumers can resolve this without loading Lua, which makes deployment
// diagnostics and future Campaign dependency provenance straightforward.
extern "C" __declspec(dllexport) const char* BZFILE_GetVersion()
{
	return BZFILE_VERSION_STRING;
}
