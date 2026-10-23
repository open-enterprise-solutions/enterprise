#ifndef _DESIGNER_FORM_EDIT_H__
#define _DESIGNER_FORM_EDIT_H__

// A FORM AS THE DESIGNER EDITS IT — the desktop designer's ibFormEditDocument and ibFormEditView
// (designer/docManager/templates/docViewFormEditor.h) over a form metaobject. ⭐ THE CLIENT EDITS IT, THE SERVER HOLDS
// IT (Max, 2026-10-15): the form is loaded here from what the metaobject keeps (the desktop editor's LoadForm) and sent
// whole when the tab opens — its node as a frame draws it, each control's rows, the classes a control may be made of;
// the client edits it as the desktop's editor did, and hands it back whole. Its module is the module editor's
// (ibModuleTextView — the same requests, the same check, the same Input and Change). Not the desktop's names:
// frmserver is loaded into the client for a file base — one wx class info per name.

#include "frmserver/docView/templates/designer/docViewModuleEdit.h"
#include "frmserver/visualView/visualHost.h"

#include <functional>
#include <set>

struct ibCommandDescription;

// The form's picture as the designer holds it — its controls built by a designer's host (IsDesignerHost); the View's
// State is the module's (Syntax, ReadOnly, Modified, Module) and the card's title (Caption).
class FRMSERVER_API ibFormDesignView : public ibModuleTextView, public ibVisualHost {
public:

	ibFormDesignView() : ibModuleTextView() {}

	// The form loaded and its controls created (the desktop's LoadForm, on the view's creation); taken through their
	// cleanup when the document closes.
	virtual bool OnCreate(ibDocument* doc, long flags) override;
	virtual void OnClosingDocument() override;

	// The module's State, and the card's title — the form itself rides the fetch: the client holds it.
	virtual void OnDraw(ibDataNode& frame) override;
	// ⭐ THE TAB OPENED IN ONE ANSWER — the module's text, and the form whole (Form, Controls, Classes, AttributeTree,
	// CommandTree); what only the server knows of the form the client edits (ibProtocol::ibView::ibFormDesigner::ibRequest) — asked about the
	// form the request carries; the module's requests by their Kind.
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;
	// ⭐ THE FORM HANDED BACK WHOLE (Change with Form) — loaded over the one held, as the desktop's editor kept it; the
	// form tested (Command TestForm, with Form) — shown as the runtime shows it, the desktop's TestForm; the module's
	// Input and Change, the module's.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;
	virtual ibProtocol::ibView::ibKind GetViewKind() const override { return ibProtocol::ibView::ibKind::FormDesigner; }

	virtual ibValueForm* GetValueForm() const override { return m_valueForm; }
	virtual bool IsDesignerHost() const override { return true; }
	// The card's title (the desktop's ibDesignerWindow) — the client draws the card, its title the View's State.Caption,
	// composed with every frame (OnDraw); nothing is kept here.
	virtual void SetCaption(const wxString& /*strCaption*/) override {}

	// THE DESKTOP EDITOR'S LOAD AND SAVE (ibVisualEditor::LoadForm / SaveForm): the form from the compile cache, else
	// from its data, else built from its kind (a new form); written back into the metaobject (SaveFormData).
	bool LoadForm();
	// …and the form as the client holds it (a node as a frame draws it) loaded over the one held — the client edits it
	// and hands it over only when it saves; anything asked about it is asked about that one.
	bool LoadForm(const ibDataNode& node);
	bool SaveForm();

private:

	// THE TAB'S REQUESTS ABOUT THE FORM (ibProtocol::ibView::ibFormDesigner::ibRequest), each on the form held: the rows
	// under an attribute row (Composition); a door of the attribute tree, of the command tree (Attribute, FormCommand); a
	// control bound to a dropped row, running a dropped command (BoundControl, CommandControl); the rows of an attribute
	// or a form command (Properties); a control's rows again (Rows), and what one of its properties' editor reads beside
	// its row (Editor).
	bool FetchComposition(const ibDataNode& request, ibDataNode& response);
	bool FetchAttribute(const ibDataNode& request, ibDataNode& response);
	bool FetchFormCommand(const ibDataNode& request, ibDataNode& response);
	bool FetchControl(const ibDataNode& request, ibDataNode& response);
	bool FetchBoundControl(const ibDataNode& request, ibDataNode& response);
	bool FetchCommandControl(const ibDataNode& request, ibDataNode& response);
	bool FetchProperties(const ibDataNode& request, ibDataNode& response);
	bool FetchRows(const ibDataNode& request, ibDataNode& response);
	bool FetchEditor(const ibDataNode& request, ibDataNode& response);

	// The form whole, as the client edits it: its node as a frame draws it (Form), its attributes and its commands as the
	// trees beside it show them (AttributeTree, CommandTree). No control's rows: the client asks them of the one it shows
	// (Rows) — every new control's rode each answer, the inspector shown or not (2026-10-22).
	void WriteForm(ibDataNode& response);
	// The classes a control may be made of (Classes — Name, Picture, Type; not offered to place: Visible false; the
	// classes a control of it adds in itself: Menu).
	void WriteClasses(ibDataNode& response);
	// A control's rows as the designer's inspector shows them, under Control, its id — its command bar's with them.
	static void WriteRows(ibValueFrame* control, ibDataNode& controls);
	// The form handed back, its properties where a reader looks for them — the wire brought each scalar back a field.
	static void RestoreProperties(ibDataNode& node);
	// A property or an event of an attribute, of a form command, given the value the request carries (Name, Value) —
	// its owner asked and told, as the inspector's door asks and tells it.
	static bool SetProperty(ibPropertyObject* object, const ibDataNode& request);
	// The column of that id of the value table an attribute holds — the property object its row shows; none: null.
	static ibPropertyObject* FindColumnById(class ibFormAttributeValue* entry, ibMetaID id);

	// THE TREES BESIDE THE FORM — the desktop's (visualEditorAttributeTree.cpp, visualEditorCommandTree.cpp): the form's
	// attributes, a row each; its commands in their three sections. A row's path read back as the client holds it.
	void FillAttributeTree(ibDataNode& node) const;
	void FillCommandTree(ibDataNode& node);
	static void ReadPath(const ibDataNode& node, ibSourceDescription& desc);
	static void ReadPath(const ibDataNode& node, ibCommandDescription& desc);

	// THE DESKTOP EDITOR'S CREATE ON A DROP (visualEditorCmdProc.cpp) — a control bound to the source at the path's end, a
	// tablebox's column for a field of it; a button running the command, a tablebox's bar item, an item of the bar it
	// was dropped on (`dropBar`; none: null); either made where the drop was and put on the form (InsertObject). The one
	// made, else none.
	ibValueFrame* CreateBoundControl(ibValueFrame* parentHint, const ibSourceDescription& desc);
	ibValueFrame* CreateCommandButton(ibValueFrame* target, const ibCommandDescription& desc,
		class ibValueCommandBar* dropBar = nullptr);
	ibValueFrame* CreateControlOfClass(ibValueFrame* parentHint, const wxString& className,
		const std::function<void(ibValueFrame*)>& bind);
	// THE DESKTOP EDITOR'S PASTE over the node the client copied (a control as the form draws it, in its sizer item; a
	// page, a column bare): put in the control pasted on, else in the first of its parents that can hold it, right after
	// it — its ids this form's (the one numbering: GenerateNewID), its names unique on it. The control put, else none.
	ibValueFrame* PasteObject(ibValueFrame* dstObject, const ibDataNode& copied);
	void InsertObject(ibValueFrame* object, ibValueFrame* parent, int pos = -1);
	static int CalcPositionOfInsertion(ibValueFrame* selected, ibValueFrame* parent);

	ibValuePtr<ibValueForm> m_valueForm;

	// Whether what the tab is opened with rode a frame already (OnDraw, State Opening) — once.
	bool m_openingDrawn = false;

	wxDECLARE_DYNAMIC_CLASS(ibFormDesignView);
};

// Its document — the form metaobject's, a module document: saved and closed past the module's check, the form written
// into the metaobject before it is saved (the desktop's OnSaveDocument → SaveForm).
class FRMSERVER_API ibFormDesignDocument : public ibModuleTextDocument {
public:

	ibFormDesignDocument() : ibModuleTextDocument() {}

	virtual bool Save() override;

	ibFormDesignView* GetFirstView() const;

	ibFormDesignDocument(const ibFormDesignDocument&) = delete;
	ibFormDesignDocument& operator=(const ibFormDesignDocument&) = delete;
	wxDECLARE_DYNAMIC_CLASS(ibFormDesignDocument);
};

#endif
