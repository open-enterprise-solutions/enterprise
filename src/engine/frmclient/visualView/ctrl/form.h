#ifndef _FRMCLIENT_VIEW_FORM_H__
#define _FRMCLIENT_VIEW_FORM_H__

#include "frame.h"

#include "frmclient/visualView/controlEnum.h"   // the form's orient
#include "frmclient/backend/propertyManager/property/propertyBoolean.h"
#include "frmclient/backend/propertyManager/property/propertyColour.h"
#include "frmclient/backend/propertyManager/property/propertyEnum.h"
#include "frmclient/backend/propertyManager/property/propertyString.h"

class ibFormVisualDocument;

// THE FORM — the desktop's ibValueForm (frontend/visualView/ctrl/form.h) as its view asks it: the frame its controls
// are drawn in, and the one of them active now. The form itself is the server's: the control made active is told
// there (Focus), as the desktop's form was told in its own process.
class ibValueForm : public ibValueFrame {
public:

	// Of its class as a control made for a node is (ibValueFrame::Make) — what its editor finds its picture by.
	explicit ibValueForm(ibVisualHostClient& host) : ibValueFrame(host, 0) { m_className = ibProtocolType::ClientForm; }

	// The form draws nothing of its own here — its host draws its look and its command bar.
	virtual void Create(wxWindow* WXUNUSED(parent), const ibProtocolNode& WXUNUSED(node)) override {}
	virtual void Update(const ibProtocolNode& WXUNUSED(node)) override {}

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// Its id is the view's — named as the view is drawn.
	void SetControlID(long long controlId) { m_controlId = controlId; }

	// Kept, never followed, until it is found among the form's own controls.
	ibValueFrame* GetActiveControl() const;
	void SetActiveControl(ibValueFrame* control);

	// — THE DESKTOP'S FORM AS ITS FORM EDITOR ASKS IT (win/dlgs/formEditor) —
	// The document of the tab the form is drawn in — the desktop's visual document; its window is what the editor opens
	// over. ⚠ Not the host: the host's children are torn down when the form is drawn anew (an Apply that moved a
	// control), and a window opened over it went with them.
	ibFormVisualDocument* GetVisualDocument() const;
	// The person's arrangement put on: the form as its controls stand now is the editor's answer (Apply); the server lays
	// it over its own form and draws it again, and its controls are described anew (ibRestoreFormSettings).
	void UpdateForm();
	// A control of the form by the id the server names it by — none: not among them.
	ibValueFrame* FindControlByID(long long controlId) const;

	// Its controls let go of — the host's drawing begins again; the form itself stays, as the desktop's did.
	void ClearChildren() { RemoveAllChildren(); m_activeControl = nullptr; }

private:

	ibPropertyCategory* m_categoryFrame = ibPropertyObject::CreatePropertyCategory(wxT("Frame"), _("Frame"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryFrame, wxT("Title"), _("Title"),
		_("The form window's title. Empty: the synonym of the object the form shows (or of the form itself) is used. Can be written per language and changed from code while the form is open."),
		wxT(""));
	ibPropertyColour* m_propertyFG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryFrame, wxT("ForegroundColour"), _("Foreground"), _("The form's text colour, inherited by controls that have no colour of their own. Default: the system colour."), wxDefaultStypeFGColour);
	ibPropertyColour* m_propertyBG = ibPropertyObject::CreateProperty<ibPropertyColour>(m_categoryFrame, wxT("BackgroundColour"), _("Background"), _("The form's background colour. Default: the system colour, which follows the user's theme."), wxDefaultStypeBGColour);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryFrame, wxT("Enabled"), _("Enabled"), _("Whether the user can interact with the form. Off: every control on it is greyed out and takes no input - a view-only form."), true);
	ibPropertyCategory* m_categorySizer = ibPropertyObject::CreatePropertyCategory(wxT("Sizer"), _("Sizer"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySizer, wxT("Orient"), _("Orient"),
		_("How the form lays out its top-level controls: vertically (the default, one under another) or horizontally (side by side). Groups inside the form have their own orientation."),
		wxVERTICAL);

	ibValueFrame* m_activeControl = nullptr;
};

#endif
