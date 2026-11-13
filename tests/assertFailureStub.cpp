// oes_string_tests compiles fstring.cpp in, without core.dll (tests/CMakeLists.txt). Format's refusal asserts through
// ibAssertFailure, whose home is the crash guard (core/diagnostics/crashGuard.cpp) with the journal behind it - more
// than this target links. Here a failed assertion goes on, as the engine's does once it has been said.

#include "core/coreApi.h"

void ibAssertFailure(const char* /*file*/, int /*line*/, const char* /*func*/, const char* /*cond*/, const ibString& /*msg*/)
{
}
