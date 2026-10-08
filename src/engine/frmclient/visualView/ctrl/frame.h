#ifndef _FRMCLIENT_VIEW_CONTROL_H__
#define _FRMCLIENT_VIEW_CONTROL_H__

#include <functional>
#include <memory>
#include <vector>

#include <wx/wx.h>

#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

#include "frmclient/backend/compiler/value.h"                   // a control is a counted value, held by its parent
#include "frmclient/backend/backend_form.h"                     // ibBackendControlFrame — a control that holds a value
#include "frmclient/backend/propertyManager/propertyObject.h"   // a control is a property object, as the desktop's is
#include "frmclient/visualView/formdefs.h"                      // COMPONENT_TYPE_* — what a control is in the form's tree

class ibVisualHostClient;
class ibValueForm;
class ibDataViewColumnGroup;
class ibFrontendView;

// Default foreground / background for designer-created form controls,
// toolbars, dataviews, dialogs. Aligned with interior palette: deep
// dusty blue text on cream content surface. Was Windows-blue accent
// (#0078D7) + light off-white grey (#EBEBF1) - both clashed with the
// powder-blue + cream + terracotta palette.
#define wxDefaultStypeFGColour wxColour(0x3F, 0x5C, 0x77)  // #3F5C77 deep dusty blue
#define wxDefaultStypeBGColour wxColour(0xFA, 0xF7, 0xF0)  // #FAF7F0 cream content

class ibFormVisualDocument;

// A CONTROL THAT HOLDS A VALUE — the desktop's ibControlFrame (frontend/visualView/ctrl/frame.h): what the shared value
// choice (ibTypeControlFactory::ChooseValue) reads the current value through and puts the chosen one back through. A
// settings window's value cell is one.
class FRMCLIENT_API ibControlFrame : public ibBackendControlFrame {
public:

	//get value control and guid
	virtual bool GetControlValue(ibValue& WXUNUSED(pvarControlVal)) const override { return false; }
	// ...AND WRITE IT BACK. Default arg = clear (what "empty" means is the type's business).
	virtual bool SetControlValue(const ibValue& WXUNUSED(varControlVal) = ibValue()) { return false; }
	virtual ibGuid GetControlGuid() const override { return ibGuid::newGuid(); }

	virtual ibValueForm* GetOwnerForm() const { return nullptr; }
	virtual ibClassID GetClassType() const override { return 0; }
	virtual ibFormVisualDocument* GetVisualDocument() const { return nullptr; }

	virtual bool HasQuickChoice() const = 0;
	virtual void ChoiceProcessing(ibValue& vSelected) = 0;

	// THE CHOICE'S WINDOW WENT — chosen or not (the client's): a calculator or a calendar drawn for a server's question
	// answers it then (ibProtocolRequestKind::SimpleChoice).
	virtual void ChoiceDismissed() {}
};

// What the person is typing and has not given yet — the text, and where the caret stands in it.
struct ibViewEdit {
	wxString text;
	long     insertion = 0;
};

// A fetch of a control's — a table's rows, a spreadsheet's cells: the Request asked, the answer as it came; false —
// refused. It speaks to the communicator alone, so it may be called from a thread of the control's own.
using ibViewFetcher = std::function<bool(const ibProtocolNode& request, ibProtocolNode& answer)>;

// A CONTROL OF A VIEW — one node of the tab's form as this client draws it: the desktop's ibValueFrame
// (frontend/visualView/ctrl) cut down to its drawing half, Create and Update. What it shows comes from the node — what
// the form saved it with, and its State, what the server reads it as now; what the person does with it goes back to
// the server as an event, by the control's id. It keeps no value of its own: the next frame says what it shows.
//
// No handle into the frame is kept past a draw — the next answer changes the frame under it; what a control needs
// between draws it copies.
//
// THE FORM'S TREE HOLDS ITS CONTROLS, as the desktop's does (ibPropertyObjectHelper): a parent holds its children — their
// order, where one moves to — and lets them go with it.
class ibValueFrame : public ibValue,
	public ibPropertyObjectHelper<ibValueFrame> {
public:

	// The control drawn for a node of that type — none for a type this client does not draw yet; held once it is added to
	// its parent.
	static ibValueFrame* Make(const wxString& type, ibVisualHostClient& host, long long controlId);

	virtual ~ibValueFrame() = default;

	// — THE DESKTOP'S CONTROL AS ITS FORM EDITOR ASKS IT (win/dlgs/formEditor) —
	// The properties a person may arrange for themselves — the desktop's list.
	static wxArrayString GetAllowedUserProperty();

	virtual wxString GetClassName() const override { return m_className; }
	virtual wxString GetObjectTypeName() const override { return m_className; }
	virtual bool IsEditable() const override { return true; }

	// Its name — the desktop's: a control keeps it as its Name property (ibValueControl).
	wxString GetControlName() const {
		wxString result;
		GetControlNameAsString(result);
		return result;
	}
	void SetControlName(const wxString& name) { SetControlNameAsString(name); }
	virtual bool GetControlNameAsString(wxString& result) const {
		result = GetObjectTypeName();
		return true;
	}
	virtual bool SetControlNameAsString(const wxString& WXUNUSED(result)) const { return false; }
	// What a person reads it as in the form's tree — the desktop's GetControlTitle, answered by the server where the
	// metadata is (its State's Caption, a form's or a page's Title: the synonym of what it is bound to when no Title is
	// set); none said — its class.
	virtual wxString GetControlTitle() const { return !m_title.IsEmpty() ? m_title : wxGetTranslation(m_className); }
	void SetControlTitle(const wxString& title) { m_title = title; }

	// Open in the object tree; offered at all (the base's functional options are the server's: what it draws is offered).
	void SetExpanded(bool expanded) { m_expanded = expanded; }
	bool GetExpanded() const { return m_expanded; }
	virtual bool IsAvailable() const { return true; }

	// ITS PROPERTIES AS THE FRAME CARRIES THEM — the desktop's SaveNode shape, written by the server's control
	// (WriteData) and read here by the same per-type code; the form editor's Apply writes them back.
	virtual bool ReadData(const ibDataNode& WXUNUSED(node)) { return true; }
	virtual bool WriteData(ibDataNode& WXUNUSED(node)) const { return true; }

	// A child moved among the others — the helper's mechanism, opened by the owner (ibPropertyObjectHelper).
	bool ChangeChildPosition(ibValueFrame* obj, unsigned int pos);

	// Built in the window, from the node; Update then draws what it shows — and draws it again for each frame of the
	// same shape.
	virtual void Create(wxWindow* parent, const ibProtocolNode& node) = 0;
	virtual void Update(const ibProtocolNode& node) = 0;
	// …and only what the frame's patch changed in it (`patch` — its node there): drawn whole, unless the control knows
	// which of its parts a patch names.
	virtual void Update(const ibProtocolNode& node, const ibProtocolNode& /*patch*/) { Update(node); }

	// What it is in the layout — a window or a sizer.
	virtual wxWindow* GetWindow() const { return nullptr; }
	virtual wxSizer*  GetSizer() const { return nullptr; }
	// The window what is in it is built in: its own, a framed group's box; none — the one it was built in.
	virtual wxWindow* GetChildParent() const { return GetWindow(); }
	// The group a table's column built in it hangs on: a table's root group, a column group; none — no table.
	virtual ibDataViewColumnGroup* GetColumnHolder() const { return nullptr; }
	// The view it holds of its own — a grid box's sheet, a text box's text (the desktop's GetControlView): what the
	// form's view hands its menu, toolbar, commands and undo to while it is the active control. None — a bare control.
	virtual ibFrontendView* GetControlView() const { return nullptr; }

	// An edit under way, carried over a rebuild into the control built anew for the same node: a frame that changes
	// the form under the person's hands loses nothing of what is typed, and commits nothing of it.
	virtual bool SaveEdit(ibViewEdit& /*edit*/) const { return false; }
	virtual void RestoreEdit(const ibViewEdit& /*edit*/) {}

	// Its id — what the server names it by.
	long long GetControlID() const { return m_controlId; }

protected:

	ibValueFrame(ibVisualHostClient& host, long long controlId) : m_host(host), m_controlId(controlId) {}

	// What the person did, to the server — Args, what the event says with it.
	void Send(ibProtocolEvent event) const;
	void Send(ibProtocolEvent event, const ibProtocolNode& args) const;

	// Its fetches — kept by whoever reads with them, on whichever thread.
	ibViewFetcher MakeFetcher() const;

	// The desktop's UpdateWindow: the sizes and the look the form saved; enabled, shown and the tooltip as State says.
	static void UpdateWindow(wxWindow* window, const ibProtocolNode& node);
	// The desktop's ApplyLook: the font and the colours, where the form set them.
	static void ApplyLook(wxWindow* window, const ibProtocolNode& node);

private:

	friend class ibValueForm;   // the control made active is told to the server as its own event

	ibVisualHostClient& m_host;
	long long           m_controlId;   // a form's — the view's, as it is drawn (ibValueForm::SetControlID)

	wxString m_className;      // its node's type — the server's class name
	wxString m_title;          // what the server says it is called (GetControlTitle)
	bool     m_expanded = true;
};

#endif
