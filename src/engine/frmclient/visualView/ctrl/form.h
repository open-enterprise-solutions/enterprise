#ifndef _FRMCLIENT_VIEW_FORM_H__
#define _FRMCLIENT_VIEW_FORM_H__

#include "frame.h"

class ibTabDocument;

// THE FORM — the desktop's ibValueForm (frontend/visualView/ctrl/form.h) as its view asks it: the frame its controls
// are drawn in, and the one of them active now. The form itself is the server's: the control made active is told
// there (Focus), as the desktop's form was told in its own process.
class FRMCLIENT_API ibValueForm : public ibValueFrame {
public:

	// Of its class as a control made for a node is (ibValueFrame::Make) — what its editor finds its picture by.
	explicit ibValueForm(ibVisualHostClient& host) : ibValueFrame(host, 0) { m_className = ibProtocol::ibForm::ibType::ClientForm; }

	// The form draws nothing of its own here — its host draws its look and its command bar.
	virtual void Create(wxWindow* WXUNUSED(parent), const ibProtocolNode& WXUNUSED(node)) override {}
	virtual void Update(const ibProtocolNode& WXUNUSED(node)) override {}
	// …so its command bar is the host's.
	virtual ibViewCommandBar* GetCommandBar() const override;

	// Its id is the view's — named as the view is drawn.
	void SetControlID(long long controlId) { m_controlId = controlId; }

	// Kept, never followed, until it is found among the form's own controls.
	ibValueFrame* GetActiveControl() const;
	void SetActiveControl(ibValueFrame* control);

	// — THE DESKTOP'S FORM AS ITS FORM EDITOR ASKS IT (win/dlgs/formEditor) —
	// The document of the tab the form is drawn in — the desktop's visual document; its window is what the editor opens
	// over. ⚠ Not the host: the host's children are torn down when the form is drawn anew (an Apply that moved a
	// control), and a window opened over it went with them.
	ibTabDocument* GetVisualDocument() const;
	// The person's arrangement put on: the form as its controls stand now is the editor's answer (Apply); the server lays
	// it over its own form and draws it again, and its controls are described anew (ibRestoreFormSettings).
	void UpdateForm();
	// A control of the form by the id the server names it by — none: not among them.
	ibValueFrame* FindControlByID(long long controlId) const;

	// Its controls let go of — the host's drawing begins again; the form itself stays, as the desktop's did.
	void ClearChildren() { RemoveAllChildren(); m_activeControl = nullptr; }

private:

	ibValueFrame* m_activeControl = nullptr;
};

#endif
