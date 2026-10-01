////////////////////////////////////////////////////////////////////////////
//	Description : the caller's own clipboard — storage only
////////////////////////////////////////////////////////////////////////////

#include "mcpClipboard.h"

#include <map>

wxString ibMcpClipboardKindName(ibMcpClipboardKind kind)
{
	switch (kind) {
	case ibMcpClipboardKind::Metadata: return wxT("metadata object");
	case ibMcpClipboardKind::Control: return wxT("form control");
	case ibMcpClipboardKind::Cells: return wxT("cells");
	default: return wxT("nothing");
	}
}

// The default slot has a NAME rather than a separate variable, so "one buffer"
// and "several buffers" are the same code path with the same rules.
static const wxChar* kDefaultSlot = wxT("default");

static std::map<wxString, ibMcpClipboardSlot>& Board()
{
	// Function-local static: constructed on first use, so no other translation
	// unit's static initialiser can reach it before it exists.
	static std::map<wxString, ibMcpClipboardSlot> s_board;
	return s_board;
}

ibMcpClipboardSlot& ibMcpClipboard(const wxString& slot)
{
	return Board()[slot.IsEmpty() ? wxString(kDefaultSlot) : slot];
}

std::vector<wxString> ibMcpClipboardSlots()
{
	std::vector<wxString> names;
	for (const auto& pair : Board()) {
		if (!pair.second.IsEmpty())
			names.push_back(pair.first);
	}
	return names;
}
