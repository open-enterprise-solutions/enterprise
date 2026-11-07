#ifndef _FRMCLIENT_KIND_SELECTOR_H__
#define _FRMCLIENT_KIND_SELECTOR_H__

#include "gui/univ/dialog.h"
#include "gui/univ/ctrl/button.h"
#include <wx/sizer.h>
#include <wx/radiobox.h>
#include "gui/univ/ctrl/staticText.h"
#include "gui/univ/ctrl/textCtrl.h"

#include <vector>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

// THE KIND OF AN OBJECT JUST MADE — a form's, the old desktop's ibDialogSelectTypeForm, and a template's. What it offers
// the server sent (ibProtocol::ibRequest::ibKind::ObjectKind) — the window's Caption, the object's Name, Synonym and
// Comment, and a kind per child: its Id, Caption and the Name the object takes with it; the desktop's read them of the
// configuration. The server puts the answer on the object, as the desktop's OK put it there. Here, as the desktop keeps
// it; asked by the designer's frame (ibFrontendMainFrameDesigner::ShowRequest).
class FRMCLIENT_API ibDialogSelectKind : public ibDialog {

	ibStaticText* m_staticTextName;
	ibStaticText* m_staticTextSynonym;
	ibStaticText* m_staticTextComment;
	ibTextCtrl* m_textCtrlName;
	ibTextCtrl* m_textCtrlSynonym;
	ibTextCtrl* m_textCtrlComment;
	ibButton* m_sdbSizerOK;
	ibButton* m_sdbSizerCancel;

	struct ibChoiceData {
		wxString m_name;
		wxString m_label;
		int		 m_value;
		ibChoiceData(const wxString& name, const wxString& label,
			const int& val) : m_name(name), m_label(label),
			m_value(val)
		{
		}
	};

	std::vector<ibChoiceData> m_listChoice;
	int m_choice = 0;   // a generic form's is wxNOT_FOUND

public:

	ibDialogSelectKind(const ibProtocolNode& request);
	virtual ~ibDialogSelectKind();

	// The kind chosen and the texts typed, as the answer carries them (Id, Name, Synonym, Comment); false — cancelled.
	bool ShowModal(ibProtocolNode& response);

protected:

	void OnTextEnter(wxCommandEvent& event);
	void OnButtonOk(wxCommandEvent& event);
	void OnButtonCancel(wxCommandEvent& event);
	void OnKindChanged(wxCommandEvent& event);
};

#endif
