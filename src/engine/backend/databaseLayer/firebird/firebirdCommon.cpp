#include "firebirdCommon.h"

#include <cstdio>

#ifdef __IBMSW__
#include <windows.h>
#endif

namespace ibFb {

void LogThreadSafe(const ibString& msg) {
#ifdef __IBMSW__
	const ibString line = msg + ibT("\n");
	::OutputDebugStringW(line.wc_str());
#else
	const auto utf8 = msg.utf8_str();
	std::fputs(utf8.data(), stderr);
	std::fputc('\n', stderr);
#endif
}

} // namespace ibFb
