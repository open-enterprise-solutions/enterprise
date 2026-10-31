#ifndef _METATREE_CLIPBOARD_LOCK_H__
#define _METATREE_CLIPBOARD_LOCK_H__

////////////////////////////////////////////////////////////////////////////
// THE CLIPBOARD IS A LOCK, so it is taken as one.
////////////////////////////////////////////////////////////////////////////
//
// On MSW an Open() that never reaches its Close() holds the clipboard for EVERY application in
// the session until the process exits — it is an OS-wide lock, not a local handle. All three
// metadata trees closed it inside the branch that succeeded, so two ordinary gestures leaked it:
//
//   * Ctrl+C on a GROUP node — the node carries a class id and no metaobject, so the "there is
//     something to copy" branch is skipped, and with it the Close().
//   * Ctrl+V with anything that is not an OES metaobject on the clipboard — `Open()` had already
//     succeeded by the time `IsSupported()` said no, and the Close() sat past that test.
//
// A guard cannot forget, which is the whole point: the destructor runs on every path out,
// including the ones nobody wrote down.
//
////////////////////////////////////////////////////////////////////////////

#include <wx/clipbrd.h>

class ibClipboardLock {
public:
	ibClipboardLock() : m_open(wxTheClipboard != nullptr && wxTheClipboard->Open()) {}
	~ibClipboardLock() { if (m_open) wxTheClipboard->Close(); }

	// Whether the clipboard is ours right now. False is a normal answer — another application
	// may be holding it — and the caller simply does nothing.
	bool IsOpen() const { return m_open; }

	ibClipboardLock(const ibClipboardLock&) = delete;
	ibClipboardLock& operator=(const ibClipboardLock&) = delete;

private:
	bool m_open;
};

#endif
