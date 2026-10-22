////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "titleFrame.h"

#include "core/diagnostics/journal.h"
#include "designer-thin/docManager/templates/docViewModuleEditor.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolValue — a control's values, as its node has them
#include "frmclient/mainFrame/objinspect/objinspect.h"
#include "frmclient/propertyManager/property/advprop/advpropSource.h"   // ibSetSourcePicker — a control's Source cell

#include <set>

namespace {

// The form editors open — a control's Source cell asks the one whose control it is for its attribute tree.
std::set<ibVisualEditorNotebook::ibVisualEditor*>& OpenEditors()
{
	static std::set<ibVisualEditorNotebook::ibVisualEditor*> editors;
	return editors;
}

}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditor(ibModuleEditDocument* document, ibVisualEditorNotebook* parent, int id) :
	wxPanel(parent, id), m_document(document), m_visualNotebook(parent)
{
	CreateWideGui();

	// THE SOURCE WINDOW — opened by the form whose control the cell is of (the one the inspector shows).
	static const bool picker = (ibSetSourcePicker([](wxWindow* parent, const ibPropertyObject* owner, const wxString& name,
		ibProtocol::ibSchema::ibProperties::ibSourceDataType filter, ibSourceDescription& binding) {
		for (ibVisualEditor* const editor : OpenEditors()) {
			if (owner != nullptr && editor->GetCurrentElement() == owner)
				return editor->GetAttributeTree()->PickSource(parent, name, filter, binding);
		}
		return false;
	}), true);
	(void)picker;
	OpenEditors().insert(this);
}

void ibVisualEditorNotebook::ibVisualEditor::CreateWideGui()
{
	wxWindow::Freeze();

	m_splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_THIN_SASH);
	m_splitter->SetSashGravity(1.0);   // the CANVAS absorbs vertical growth; the tree strip below keeps a fixed height
	m_splitter->SetMinimumPaneSize(20); // Smalest size the

	wxBoxSizer* sizerMain = new wxBoxSizer(wxVERTICAL);
	sizerMain->Add(m_splitter, 1, wxEXPAND, 0);

	wxASSERT(m_visualEditor == nullptr);

	m_visualEditor = new ibVisualEditorHost(this, m_splitter, m_document->GetTabId());

	// Bottom pane: the control tree and the source-attribute tree side by side,
	// separated by their own splitter.
	// The three navigators read best as ROUGHLY EQUAL THIRDS (Tree elements | Attributes | Commands). The outer
	// split gives the object tree ~1/3 (gravity 0.34 -> it keeps a third of any horizontal growth, the right pane
	// two thirds); the inner split halves the remainder. A generous min pane size (a readable column, not 20 px)
	// stops the Commands pane from collapsing to a sliver on a wide window.
	m_treeSplitter = new wxSplitterWindow(m_splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_THIN_SASH);
	m_treeSplitter->SetSashGravity(0.34);
	m_treeSplitter->SetMinimumPaneSize(160);

	m_objectTree = new ibVisualEditorObjectTree(this, m_treeSplitter);

	// The right pane splits again: [Attributes | Commands] — the source-attribute tree and the command
	// navigator (the command door beside the data door). The command navigator is where a designer drags
	// a command onto the form to create a bound button. Equal halves of the remaining two thirds.
	m_rightSplitter = new wxSplitterWindow(m_treeSplitter, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_THIN_SASH);
	m_rightSplitter->SetSashGravity(0.5);
	m_rightSplitter->SetMinimumPaneSize(160);
	m_attributeTree = new ibVisualEditorAttributeTree(this, m_rightSplitter);
	m_commandTree = new ibVisualEditorCommandTree(this, m_rightSplitter);
	m_rightSplitter->SplitVertically(
		ibPanelTitle::CreateTitle(m_attributeTree, _("Attributes")),
		ibPanelTitle::CreateTitle(m_commandTree, _("Commands")), 320);

	m_treeSplitter->SplitVertically(
		ibPanelTitle::CreateTitle(m_objectTree, _("Tree elements")),
		m_rightSplitter, 320);

	m_splitter->SplitHorizontally(m_visualEditor, m_treeSplitter, 200);

	SetSizer(sizerMain);

	// A wxSplitterWindow's sash set at construction lands on a not-yet-sized window, so the panes render lopsided
	// (the Commands third collapses to a sliver, the canvas/tree split is arbitrary). Set the three sashes ONCE the
	// panel has its first REAL size — canvas over a fixed tree strip, and the three navigators as equal thirds — then
	// let gravity hold the balance on every later resize. One-shot (m_layoutBalanced), so a manual drag afterwards sticks.
	Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		event.Skip();
		if (m_layoutBalanced)
			return;
		const wxSize sz = GetClientSize();
		if (sz.GetWidth() < 480 || sz.GetHeight() < 320)   // wait for the first genuine layout pass
			return;
		m_layoutBalanced = true;
		Layout();   // the size event comes before the sizer pass: size the splitters first, or the sashes clamp to their old size
		m_splitter->SetSashPosition(sz.GetHeight() - 260);   // canvas on top; a ~260px navigator strip below
		m_treeSplitter->SetSashPosition(sz.GetWidth() / 3);  // object tree = LEFT third
		m_rightSplitter->SetSashPosition(sz.GetWidth() / 3); // attributes = MIDDLE third, commands = RIGHT third
	});

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	wxWindow::Thaw();
}

void ibVisualEditorNotebook::ibVisualEditor::Activate() const
{
	if (m_document != nullptr)
		m_document->Activate();
}

void ibVisualEditorNotebook::ibVisualEditor::ActivateEditor()
{
	// PASSIVE restore — re-light the inspector on the current element (the desktop's): a control, an attribute, a command,
	// a layer — whichever was selected last; soft: re-activating never pops it open.
	ibPropertyObject* const element = GetCurrentElement();
	objectInspector->SelectObject(element != nullptr ? element : GetSelectedObject());
}

bool ibVisualEditorNotebook::ibVisualEditor::LoadForm(const ibProtocolNode& answer)
{
	const ibProtocolNode form = answer.FindChild(ibProtocol::ibView::ibFormDesigner::Form);
	if (form.GetType() != ibProtocol::ibForm::ibType::ClientForm) {
		ibJournalWarning(wxT("designer"), wxT("form editor: the tab's answer brought no form"));
		return false;
	}

	m_classes = answer.FindChild(ibProtocol::ibView::ibFormDesigner::Classes);
	m_objectTree->CreateTree(m_classes);

	// Its trees; no control's rows held before — each asked as it is shown (FetchRows).
	m_rows.clear();
	m_rowsChanged.clear();
	ibProtocolNode trees;
	Receive(answer, trees);

	m_cmdProc.Reset();
	Draw(form, trees, ibProtocolNode(), form.GetInt(ibProtocol::ibForm::ControlId));
	if (GetValueForm() == nullptr)
		return false;

	NotifyEditorLoaded();
	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::Draw(const ibProtocolNode& form, const ibProtocolNode& trees,
	const ibProtocolNode& patch, long long selected)
{
	// The trees first — a holder whose row is gone is let go of before the form's controls are.
	m_trees = trees;
	m_attributeTree->Draw(m_trees.FindChild(ibProtocol::ibView::ibFormDesigner::AttributeTree), form.FindChild(ibProtocol::ibView::ibFormDesigner::Attributes));
	m_commandTree->Draw(m_trees.FindChild(ibProtocol::ibView::ibFormDesigner::CommandTree));

	// ⚠ A PATCH THAT NAMES NOTHING IS NONE — the form drawn whole: a command's ibProtocolNode() is a node of no entries,
	// and drawn as a patch it drew nothing (a control added, never shown — 2026-10-15). What the canvas is given for it is
	// no node at all — a name an empty node does not have.
	const bool patched = patch.IsNode() && !patch.Entries().empty();
	const ibProtocolNode drawn = patched ? patch : patch.FindChild(ibProtocol::ibFrame::Patch);

	// A form drawn whole may be made of other controls — the inspector lets go of one of this form's before they go.
	if (!patched) {
		ibValueFrame* const shown = GetSelectedObject();
		if (shown != nullptr && objectInspector->GetSelectedObject() == shown)
			objectInspector->SelectObject(nullptr);
	}

	m_visualEditor->Freeze();

	// The canvas never paints from a control that is gone.
	m_formNode = form;
	// ⭐ …AND THE SERVER HOLDS IT BEFORE ANYTHING HERE ASKS — the inspector's attribute received anew below, a tree's
	// composition: a request carries no form, it is asked about the one handed. As the desktop's wrote it into its
	// metaobject after each command (NotifyEditorSaved → SaveFormData).
	SaveForm();
	m_visualEditor->ClearObjectSelect();
	// ⭐ EACH CONTROL READS WHAT IT HOLDS AS THE HOST DRAWS IT — its rows and its values where its node changed
	// (ibVisualEditorHost::ReadControl): one walk and one compare, the host's. The editor walked the form again after it,
	// comparing it with the one before, to read them (2026-10-22).
	m_visualEditor->Draw(m_formNode, drawn);

	if (GetValueForm() == nullptr) {
		m_visualEditor->Thaw();
		ibJournalWarning(wxT("designer"), wxT("form editor: the form was not drawn"));
		return;
	}
	// …a control whose rows alone the answer changed, after; the rows left are of no control drawn.
	Receive(m_formNode);
	m_rowsChanged.clear();

	m_visualEditor->Thaw();

	m_selected = selected;
	NotifyEditorRefresh();

	// No control current — an attribute, a command, a layer is (SetCurrentElement): shown again where it is shown, what
	// the server says of it received anew (the desktop's RefreshCurrentElement); one whose row is gone went with it.
	if (m_selected == wxNOT_FOUND) {
		ibPropertyObject* const element = GetCurrentElement();
		if (element != nullptr && objectInspector->GetSelectedObject() == element)
			objectInspector->SelectObject(element, true);
		return;
	}

	// The selection the command left — the inspector filled anew for a form drawn whole; a property edited in it stays.
	ibValueFrame* const selectedObject = GetSelectedObject();
	if (patched) {
		m_visualEditor->SetObjectSelect(selectedObject);
		NotifyObjectSelected(selectedObject);
	}
	else {
		SelectObject(selectedObject, true);
	}
}

bool ibVisualEditorNotebook::ibVisualEditor::FetchRows(long long controlId)
{
	if (m_rows.find(controlId) != m_rows.end())
		return true;

	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Rows));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, controlId);
	ibProtocolNode answer;
	if (!m_document->Fetch(request, answer))
		return false;

	// Held from here on, marked changed: the control receives them as the one shown, or as it is drawn next. The answer
	// brings no trees — the form's stay.
	ibProtocolNode trees;
	Receive(answer, trees);
	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::Receive(const ibProtocolNode& answer, ibProtocolNode& trees)
{
	// ⭐ WHAT EACH CONTROL HAS IS THE SERVER'S TO SAY — its rows, as the designer's inspector writes them; the client's
	// controls declare none (ibValueFrame). Kept by the control's id: a control made reads them as it is built, one whose
	// rows the answer changed as it is drawn.
	for (const ibProtocolNode& described : answer.FindChild(ibProtocol::ibView::ibFormDesigner::Controls).Children()) {
		const long long id = described.GetInt(ibProtocol::ibView::ibFormDesigner::Control);
		ibProtocolNode& held = m_rows[id];
		if (held != described)
			m_rowsChanged.insert(id);
		held = described;
	}

	// …and its attributes' and its commands' trees.
	trees.SetValue(ibProtocol::ibView::ibFormDesigner::AttributeTree, answer.FindChild(ibProtocol::ibView::ibFormDesigner::AttributeTree));
	trees.SetValue(ibProtocol::ibView::ibFormDesigner::CommandTree, answer.FindChild(ibProtocol::ibView::ibFormDesigner::CommandTree));
}

void ibVisualEditorNotebook::ibVisualEditor::Receive(ibValueFrame* control)
{
	// Its rows as the server last wrote them — none yet, or an answer changed them: a control that stays keeps its
	// properties. Every control's made anew at each door's answer was a quarter of a draw (2026-10-22).
	const long long controlId = control->GetControlID();
	const bool changed = m_rowsChanged.erase(controlId) != 0;
	const auto found = m_rows.find(controlId);
	if (found != m_rows.end() && (changed || control->GetPropertyCount() == 0))
		control->Receive(found->second);
}

void ibVisualEditorNotebook::ibVisualEditor::Receive(const ibProtocolNode& node)
{
	if (m_rowsChanged.empty())
		return;

	// Its rows, then its values as its node has them — the properties are made anew with the rows.
	const long long controlId = node.GetInt(ibProtocol::ibForm::ControlId);
	if (m_rowsChanged.count(controlId) != 0) {
		ibValueForm* const valueForm = GetValueForm();
		ibValueFrame* const control = controlId == valueForm->GetControlID() ? valueForm : valueForm->FindControlByID(controlId);
		if (control != nullptr) {
			Receive(control);
			Receive(control, node);
			control->SetExpanded(node.GetBool(ibProtocol::ibForm::Expanded, true));
		}
	}

	for (const ibProtocolNode& child : node.Children())
		Receive(child);
}

void ibVisualEditorNotebook::ibVisualEditor::Receive(ibPropertyObject* object, const ibProtocolNode& node)
{
	// A property's, as WriteData wrote it — read off the node by its name, the node's children not copied nor read with it.
	for (unsigned int idx = 0; idx < object->GetPropertyCount(); idx++) {
		ibProperty* const prop = object->GetProperty(idx);
		ibDataValue value;
		if (ibReadProtocolValue(node, prop->GetName(), value))
			prop->SetNodeValue(value);
	}
}

bool ibVisualEditorNotebook::ibVisualEditor::SaveForm()
{
	// Nothing done since the form was handed back (or loaded) — nothing to hand.
	if (m_cmdProc.IsAtSavePoint() || !m_formNode.IsNode())
		return true;

	// HELD, not a call of its own — as a module's text is: what reads the form on the server (Save, the next request of
	// the tab, a close) is a call, and carries it (Pending) ahead of its work; an edit asks the server nothing more.
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibView::ibFormDesigner::Form, m_formNode);
	m_document->Hold(ibProtocol::ibEvent::ibKind::Change, args);
	m_cmdProc.SetSavePoint();
	return true;
}

ibVisualEditorNotebook::ibVisualEditor::~ibVisualEditor()
{
	OpenEditors().erase(this);

	// The inspector lets go of a control of this form before the controls go.
	ibValueFrame* const selected = GetSelectedObject();
	if (selected != nullptr && objectInspector->GetSelectedObject() == selected)
		objectInspector->SelectObject(nullptr);
}
