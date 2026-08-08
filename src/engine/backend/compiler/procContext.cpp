#include "procContext.h"
#include "procUnit.h"

//*************************************************************************************************
//*                                        RunContextSmall                                        *
//*************************************************************************************************

ibRunContextSmall::~ibRunContextSmall()
{
	// Inline slots are placement-constructed, so they must be destroyed by hand
	// — including when an exception unwinds through a live frame. DestroyLocals
	// is the single place that knows which storage was used.
	DestroyLocals();
}

//*************************************************************************************************
//*                                        RunContext                                             *
//*************************************************************************************************

ibRunContext::~ibRunContext()
{
	DestroyLocals();
}

const ibByteCode* ibRunContext::GetByteCode() const
{
	return m_procUnit != nullptr ?
		m_procUnit->GetByteCode() : nullptr;
}