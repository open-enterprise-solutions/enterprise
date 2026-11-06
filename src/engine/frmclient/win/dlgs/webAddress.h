#ifndef _FRMCLIENT_WEB_ADDRESS_H__
#define _FRMCLIENT_WEB_ADDRESS_H__

#include <wx/dialog.h>
#include <wx/listctrl.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// WEB ADDRESSES — the designer's Administration → Web addresses: what the base answers on every web server that
// publishes it, beside its clients, as the server's schema holds them (frmserver/schema/designer/webAddress.h). A
// table of the entries; Add registers one — what a kind offers, or a path of a kind the person names — and Remove
// takes the chosen one off. What the base refuses the person is told in its words, and the table is read again.
class FRMCLIENT_API ibDialogWebAddress : public wxDialog {
public:

	ibDialogWebAddress(wxWindow* parent, const ibProtocolNode& shown);

private:

	// The table as the schema shows it.
	void Fill(const ibProtocolNode& shown);
	// …asked of the server again.
	void Reload();

	void OnAdd(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);

	wxListCtrl*    m_table;
	ibProtocolNode m_shown;
};

#endif
