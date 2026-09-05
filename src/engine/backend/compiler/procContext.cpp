#include "procContext.h"
#include "procUnit.h"
#include "procUnitState.h"   // ibRunStack — the slots a per-call frame reserves on
#include "session/session.h" // ibSession::GetPUState — whose stack that is

//*************************************************************************************************
//*                                          RunStack                                             *
//*************************************************************************************************

bool ibRunStack::Reserve(const long count, ibRun& outRun)
{
	if (count < 0 || count > kBlockSlots)
		return false;                      // wider than a block — the caller owns it instead

	if (m_blocks.empty())
		m_blocks.emplace_back(new ibBlock());

	// Not enough room left in the block being filled: move to the next one WHOLE
	// rather than splitting a frame across two. A frame's slots have to be
	// contiguous — the interpreter indexes them as an array — and the tail left
	// behind is at most one frame's worth, which the next Release reclaims anyway.
	if (m_top + count > kBlockSlots) {
		m_currentBlock++;
		if (m_currentBlock >= m_blocks.size())
			m_blocks.emplace_back(new ibBlock());
		m_top = 0;
	}

	ibBlock& block = *m_blocks[m_currentBlock];

	outRun.m_vals  = block.m_vals.get() + m_top;
	outRun.m_refs  = block.m_refs.get() + m_top;
	outRun.m_block = m_currentBlock;
	outRun.m_mark  = (unsigned int)m_top;

	for (long i = 0; i < count; i++)
		outRun.m_refs[i] = &outRun.m_vals[i];

	m_top += count;
	return true;
}

void ibRunStack::Release(const ibRun& run, const long count)
{
	if (run.m_mark == kNoRun)
		return;

	// EMPTIED HERE, not on the next Reserve. A slot that held a reference keeps the
	// object it pointed at alive, and a frame that has ended must not be the reason
	// something stays in memory until an unrelated call reuses the slot.
	for (long i = 0; i < count; i++)
		run.m_vals[i].Reset();

	m_currentBlock = run.m_block;
	m_top          = (long)run.m_mark;
}

//*************************************************************************************************
//*                                        RunContextSmall                                        *
//*************************************************************************************************

namespace {

	// The run stack this frame reserves on — the session's, or the sessionless
	// fallback's, which is the same answer ibProcUnitState gives for everything else
	// it holds. Frames that own their slots never ask.
	ibRunStack* RunStackForFrame()
	{
		ibProcUnitState* state = ibSession::GetPUState();
		return state != nullptr ? &state->m_runStack : nullptr;
	}
}

void ibRunContextSmall::SetLocalCount(const long varCount)
{
	// SetLocalCount can run twice for the same frame (AttachRuntime's Run(false)
	// prepare pass, then Run(true) execute), so the previous slots have to go first.
	DestroyLocals();
	m_lVarCount = varCount;

	// THE RUN STACK when this frame IS the call, its own memory when it can outlive
	// one — the lifetime the caller declared, and nothing else, decides. Reserve
	// fills the pointer row itself, so the loop below runs only on the owning path.
	// It can also refuse (a frame wider than a block), and there owning the slots is
	// the honest answer rather than a bigger block nobody else can use.
	if (m_lifetime == ibRunLifetime::PerCall) {
		ibRunStack* stack = RunStackForFrame();
		ibRunStack::ibRun run;
		if (stack != nullptr && stack->Reserve(m_lVarCount, run)) {
			m_pLocVars    = run.m_vals;
			m_pRefLocVars = run.m_refs;
			m_pRunStack   = stack;         // give it back to the one that granted it
			m_runBlock    = run.m_block;
			m_runMark     = run.m_mark;
			return;
		}
	}

	m_pRunStack   = nullptr;
	m_runMark     = ibRunStack::kNoRun;   // owned — nothing to give back
	m_pLocVars    = new ibValue[m_lVarCount];
	m_pRefLocVars = new ibValue * [m_lVarCount];

	for (long i = 0; i < m_lVarCount; i++) m_pRefLocVars[i] = &m_pLocVars[i];
}

void ibRunContextSmall::DestroyLocals()
{
	if (m_pLocVars != nullptr) {
		if (m_pRunStack != nullptr && m_runMark != ibRunStack::kNoRun) {
			ibRunStack::ibRun run;
			run.m_vals  = m_pLocVars;
			run.m_refs  = m_pRefLocVars;
			run.m_block = m_runBlock;
			run.m_mark  = m_runMark;
			m_pRunStack->Release(run, m_lVarCount);
		}
		else {
			delete[] m_pLocVars;
			delete[] m_pRefLocVars;
		}
	}
	m_pRunStack   = nullptr;
	m_runMark     = ibRunStack::kNoRun;
	m_pLocVars    = nullptr;
	m_pRefLocVars = nullptr;
	m_lVarCount   = 0;
}

//*************************************************************************************************
//*                                          RunContext                                           *
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
