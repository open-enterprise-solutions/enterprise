#include "procContext.h"
#include "procUnit.h"

//*************************************************************************************************
//*                                        RunContextSmall                                        *
//*************************************************************************************************

void ibRunContextSmall::SetLocalCount(const long varCount)
{
	// SetLocalCount can run twice for the same context (AttachRuntime's
	// Run(false) prepare pass, then Run(true) execute), so the previous slots
	// have to go first — heap frames freed, inline ones destroyed.
	DestroyLocals();
	m_lVarCount = varCount;
	if (m_lVarCount > MAX_STATIC_VAR) {
		m_pLocVars = new ibValue[m_lVarCount];
		m_pRefLocVars = new ibValue * [m_lVarCount];
	}
	else {
		m_pLocVars = reinterpret_cast<ibValue*>(m_cLocStorage);
		for (long i = 0; i < m_lVarCount; i++)
			new (m_pLocVars + i) ibValue();
		m_pRefLocVars = m_cRefLocVars;
	}
	for (long i = 0; i < m_lVarCount; i++)
		m_pRefLocVars[i] = &m_pLocVars[i];
}

void ibRunContextSmall::DestroyLocals()
{
	if (m_pLocVars != nullptr) {
		if (m_pLocVars != reinterpret_cast<ibValue*>(m_cLocStorage)) {
			delete[] m_pLocVars;
			if (m_pRefLocVars != nullptr && m_pRefLocVars != m_cRefLocVars)
				delete[] m_pRefLocVars;
		}
		else {
			for (long i = m_lVarCount; i-- > 0; )
				m_pLocVars[i].~ibValue();
		}
	}
	m_pLocVars = nullptr;
	m_pRefLocVars = nullptr;
	m_lVarCount = 0;
}

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

void ibRunContext::SetLocalCount(const long varCount)
{
	DestroyLocals();
	m_lVarCount = varCount;

	if (m_lVarCount > MAX_STATIC_VAR) {
		m_pLocVars = new ibValue[m_lVarCount];
		m_pRefLocVars = new ibValue * [m_lVarCount];
	}
	else {
		m_pLocVars = reinterpret_cast<ibValue*>(m_cLocStorage);
		for (long i = 0; i < m_lVarCount; i++)
			new (m_pLocVars + i) ibValue();
		m_pRefLocVars = m_cRefLocVars;
	}

	for (long i = 0; i < m_lVarCount; i++) m_pRefLocVars[i] = &m_pLocVars[i];

	// Reset block-scope nesting depth — frame starts at depth 0 (fn-frame /
	// module-body). OPER_CTX_BEGIN bumps, OPER_CTX_END drops.
	// SendLocalVariables filters by entry.m_scopeDepth <= m_currentScopeDepth
	// so block-locals are hidden before / after their owning `{ }`.
	m_currentScopeDepth = 0;
}

void ibRunContext::DestroyLocals()
{
	if (m_pLocVars != nullptr) {
		if (m_pLocVars != reinterpret_cast<ibValue*>(m_cLocStorage)) {
			delete[] m_pLocVars;
			if (m_pRefLocVars != nullptr && m_pRefLocVars != m_cRefLocVars)
				delete[] m_pRefLocVars;
		}
		else {
			for (long i = m_lVarCount; i-- > 0; )
				m_pLocVars[i].~ibValue();
		}
	}
	m_pLocVars = nullptr;
	m_pRefLocVars = nullptr;
	m_lVarCount = 0;
}

ibRunContext::~ibRunContext()
{
	DestroyLocals();
}

const ibByteCode* ibRunContext::GetByteCode() const
{
	return m_procUnit != nullptr ?
		m_procUnit->GetByteCode() : nullptr;
}