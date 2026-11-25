#ifndef __WEB_ADDRESS_DIALOG_H__
#define __WEB_ADDRESS_DIALOG_H__

#include "deskgui/univ/dialog.h"
#include "deskgui/univ/ctrl/listCtrl.h"

#include "protocol/protocolNode.h"

// WEB ADDRESSES — the studio's Administration → Web addresses: what the base answers on every web server that
// publishes it, beside its clients, as the server's schema holds them (frmserver/schema/studio/webAddress.h). A
// table of the entries; Add registers one — what a kind offers, or a path of a kind the person names — and Remove
// takes the chosen one off. What the base refuses the person is told in its words, and the table is read again.
class ibDialogWebAddress : public ibDialog {
public:

	ibDialogWebAddress(wxWindow* parent, const ibProtocolNode& shown);

private:

	// The table as the schema shows it.
	void Fill(const ibProtocolNode& shown);
	// …asked of the server again.
	void Reload();

	void OnAdd(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);

	ibListCtrl*    m_table;
	ibProtocolNode m_shown;   // the table's rows are its Addresses, in their order
};

#endif
