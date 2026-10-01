#ifndef _IB_MCP_CLIPBOARD_H_
#define _IB_MCP_CLIPBOARD_H_

////////////////////////////////////////////////////////////////////////////
//	Description : the caller's OWN buffer — what a copy put there, waiting
////////////////////////////////////////////////////////////////////////////
//
// ⭐ NOT THE SYSTEM CLIPBOARD, deliberately. The designer's copy goes to the OS
// one (see treeConfigurationEvent.cpp), which is a single shared slot behind an
// exclusive lock: a tool writing there would overwrite whatever the developer
// standing at the keyboard had just copied, and would block on the lock while a
// window holds it. Two callers, one board, no way to tell whose turn it is.
//
// So the machine caller gets a board of its own. Same PAYLOAD — the bytes
// ibValueMetaObject::CopyObject writes, unchanged — carried in a different
// pocket. Nothing about copy or paste is re-implemented here; this file is the
// pocket and nothing else.
//
// ⭐ ONE PAIR OF VERBS, SEVERAL FAMILIES. A metaobject and a form control answer
// the SAME two methods (CopyObject(writer) / PasteObject(reader)), so the tools
// over them are one idea twice, not two ideas. The KIND below is what keeps a
// control from being pasted into a metadata tree — a refusal that reads, rather
// than a payload that lands somewhere it cannot work.
//
// Slots are named so a caller scripting a sequence can hold two things at once
// (copy A, copy B, paste both). One name defaulted IS the single-buffer case;
// it is not a second mechanism.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/serialize/dataBuilder.h"

#include <vector>

#include <wx/string.h>

// WHAT KIND OF THING IS IN THE SLOT. A paste asks this before it reads a byte:
// the payloads are all chunked memory and would half-parse into each other.
enum class ibMcpClipboardKind {
	None,
	Metadata,		// ibValueMetaObject::CopyObject
	Control,		// ibValueFrame::CopyObject
	Cells			// a rectangle out of an ibSpreadsheetDescription
};

BACKEND_API wxString ibMcpClipboardKindName(ibMcpClipboardKind kind);

struct BACKEND_API ibMcpClipboardSlot {

	ibMcpClipboardKind m_kind = ibMcpClipboardKind::None;

	// WHAT WAS COPIED, so the caller can confirm it grabbed the right thing
	// before pasting it somewhere. A copy that answers only "ok" leaves the
	// mistake to be discovered at the far end.
	wxString m_name;
	wxString m_what;			// its class as a script spells it: "Document", "Button"

	// ⭐ THE PAYLOAD IS A NODE, whatever family it came from. Cells describe
	// themselves as nodes already; a metaobject and a control describe
	// themselves as the chunked memory their own CopyObject writes, which goes
	// in as one Binary field. One member either way — so a later "hand me the
	// copy as JSON" is this, rendered, and not a second thing to build.
	//
	// ⚠ ibReaderMemory BORROWS its bytes and never copies them, so a caller
	// reading the Binary field out must bind it to a NAMED local first: the
	// getter returns by value, and the rvalue overload is deleted precisely
	// because that mistake reads freed memory (see fs.h).
	ibDataNode m_payload;

	bool IsEmpty() const { return m_kind == ibMcpClipboardKind::None; }
};

// THE BOARD. Returns the named slot, creating it empty on first ask, and lives
// for the process — a function-local static, the shape this tree uses wherever
// static-initialisation order across modules would otherwise decide.
BACKEND_API ibMcpClipboardSlot& ibMcpClipboard(const wxString& slot = wxEmptyString);

// Every slot that holds something, for a caller that lost track.
BACKEND_API std::vector<wxString> ibMcpClipboardSlots();

#endif // !_IB_MCP_CLIPBOARD_H_
