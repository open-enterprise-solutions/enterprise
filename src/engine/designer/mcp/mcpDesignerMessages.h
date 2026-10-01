#ifndef __IB_MCP_DESIGNER_MESSAGES_H__
#define __IB_MCP_DESIGNER_MESSAGES_H__

#include "backend/system/systemEnum.h"

#include <wx/string.h>

#include <vector>

// WHAT THE PLATFORM SAYS, KEPT AND ANNOUNCED — on this side, not in the window.
//
// Message() is the platform's non-fatal voice: a metaobject saying why it cannot
// be stored, a refused save, a modal warning, a script's own Message(), and
// everything the RUNNING APPLICATION reports back over the debugger. All of it
// arrives at the designer's window, and the window paints it into a pane.
//
// Painted, it is readable by a person and by nothing else — so an assistant
// building a configuration reads its own silence as success. That is how a chart
// of accounts with no chart of characteristic types came back as finished
// (2026-08-30).
//
// ⭐ THE WINDOW STAYS THIN. It has one job — show the message — and it now also
// hands it here in one line. Everything else (keeping the history, holding the
// listeners, deciding what is worth waking anybody for) belongs to whoever wants
// the messages, which is this side. A window that grew a notifier list and a
// bounded history would be a window doing somebody else's work.
class ibDesignerMessages {
public:

	struct Message {
		wxString        m_text;
		ibStatusMessage m_status = ibStatusMessage::ibStatusMessage_Information;
		wxString        m_docPath;      // the module, when it came from one
		long            m_line = wxNOT_FOUND;
		bool            m_modal = false;// it stopped the person and waited
	};

	class Listener {
	public:
		virtual ~Listener() = default;
		virtual void OnMessage(const Message& message) = 0;
	};

	// Called by the window's overrides, on the way to the pane.
	static void Report(const Message& message);

	static const std::vector<Message>& All();
	static void Clear();

	static void AddListener(Listener* listener);
	static void RemoveListener(Listener* listener);
};

#endif
