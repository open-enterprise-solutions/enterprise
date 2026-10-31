////////////////////////////////////////////////////////////////////////////
//	Description : the caller's own clipboard — storage only
////////////////////////////////////////////////////////////////////////////

#include "mcpClipboard.h"

#include <map>

ibString ibMcpClipboardKindName(ibMcpClipboardKind kind)
{
	switch (kind) {
	case ibMcpClipboardKind::Metadata: return ibT("metadata object");
	case ibMcpClipboardKind::Control: return ibT("form control");
	case ibMcpClipboardKind::Cells: return ibT("cells");
	default: return ibT("nothing");
	}
}

// The default slot has a NAME rather than a separate variable, so "one buffer"
// and "several buffers" are the same code path with the same rules.
static const ibChar* kDefaultSlot = ibT("default");

static std::map<ibString, ibMcpClipboardSlot>& Board()
{
	// Function-local static: constructed on first use, so no other translation
	// unit's static initialiser can reach it before it exists.
	static std::map<ibString, ibMcpClipboardSlot> s_board;
	return s_board;
}

ibMcpClipboardSlot& ibMcpClipboard(const ibString& slot)
{
	return Board()[slot.IsEmpty() ? ibString(kDefaultSlot) : slot];
}

std::vector<ibString> ibMcpClipboardSlots()
{
	std::vector<ibString> names;
	for (const auto& pair : Board()) {
		if (!pair.second.IsEmpty())
			names.push_back(pair.first);
	}
	return names;
}
