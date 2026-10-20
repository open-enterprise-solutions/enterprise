////////////////////////////////////////////////////////////////////////////
//	Description : how the query's lexer and parser refuse (queryRefusal.h)
////////////////////////////////////////////////////////////////////////////

#include "queryRefusal.h"

#include "core/exception.h"   // ibCoreException — the core's own refusal

namespace {

// The core's own: a client's handler catches ibCoreException and reads the message.
class ibQueryCoreRefusal : public ibCoreException {
public:
	explicit ibQueryCoreRefusal(const wxString& message) : ibCoreException(message) {}
};

ibQueryRefusal::ibRaise& Raise()
{
	static ibQueryRefusal::ibRaise s_raise = nullptr;
	return s_raise;
}

} // namespace

void ibQueryRefusal::Syntax(unsigned int line, unsigned int col, const wxString& message)
{
	if (Raise() != nullptr)
		Raise()(line, col, message);
	// The side's raise does not return; without one the refusal is the core's.
	throw ibQueryCoreRefusal(message);
}

void ibQueryRefusal::SetRaise(ibRaise raise)
{
	Raise() = raise;
}
