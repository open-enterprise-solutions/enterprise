#ifndef __TYPE_NAME_H__
#define __TYPE_NAME_H__

#include "core/core.h"

#include <typeinfo>

#include <wx/string.h>

// THE NAME OF A CLASS AS THE SOURCE WRITES IT — `ibPropertyBoolean`: not MSVC's "class ibPropertyBoolean", not GCC's
// or Clang's mangled one; a template by its own name, without its arguments (`ibPropertyEnum`, whatever it enumerates).
// What a property's row is shown by on the client: the server names the class of a property it draws, and the client
// names the class of one it holds itself, by this one rule.
CORE_API wxString ibTypeInfoName(const std::type_info& type);

#endif
