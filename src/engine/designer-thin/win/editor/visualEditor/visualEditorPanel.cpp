////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "titleFrame.h"

#include "core/diagnostics/journal.h"
#include "designer-thin/docManager/templates/docViewModuleEditor.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode — a control's values, as its node has them
#include "frmclient/mainFrame/objinspect/objinspect.h"

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditor(ibModuleEditDocument* document, ibVisualEditorNotebook* parent, int id) :
	wxPanel(parent, id), m_document(document), m_visualNotebook(parent)
{
	CreateWideGui();
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
	const ibProtocolNode form = answer.FindChild(ibProtocolName::Form);
	if (form.GetType() != ibProtocolType::ClientForm) {
		ibJournalWarning(wxT("designer"), wxT("form editor: the tab's answer brought no form"));
		return false;
	}

	m_classes = answer.FindChild(ibProtocolName::Classes);
	m_objectTree->CreateTree(m_classes);

	// ⭐ WHAT EACH CONTROL HAS IS THE SERVER'S TO SAY — its rows, as the designer's inspector writes them; the client's
	// controls declare none (ibValueFrame). Kept by the control's id: a control built anew reads them again.
	m_rows.clear();
	for (const ibProtocolNode& described : answer.FindChild(ibProtocolName::Controls).Children())
		m_rows[described.GetInt(ibProtocolName::Control)] = described;

	// …and its attributes' and its commands' trees.
	ibProtocolNode trees;
	trees.SetValue(ibProtocolName::AttributeTree, answer.FindChild(ibProtocolName::AttributeTree));
	trees.SetValue(ibProtocolName::CommandTree, answer.FindChild(ibProtocolName::CommandTree));

	m_cmdProc.Reset();
	Draw(form, trees, ibProtocolNode(), form.GetInt(ibProtocolName::ControlId));
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
	m_attributeTree->Draw(m_trees.FindChild(ibProtocolName::AttributeTree), form.FindChild(ibProtocolName::Attributes));
	m_commandTree->Draw(m_trees.FindChild(ibProtocolName::CommandTree));

	// ⚠ A PATCH THAT NAMES NOTHING IS NONE — the form drawn whole: a command's ibProtocolNode() is a node of no entries,
	// and drawn as a patch it drew nothing (a control added, never shown — 2026-10-15). What the canvas is given for it is
	// no node at all — a name an empty node does not have.
	const bool patched = patch.IsNode() && !patch.Entries().empty();
	const ibProtocolNode drawn = patched ? patch : patch.FindChild(ibProtocolName::Patch);

	// A form drawn whole may be made of other controls — the inspector lets go of one of this form's before they go.
	if (!patched) {
		ibValueFrame* const shown = GetSelectedObject();
		if (shown != nullptr && objectInspector->GetSelectedObject() == shown)
			objectInspector->SelectObject(nullptr);
	}

	m_visualEditor->Freeze();

	// The canvas never paints from a control that is gone.
	m_formNode = form;
	m_visualEditor->ClearObjectSelect();
	m_visualEditor->Draw(m_formNode, drawn);

	if (GetValueForm() == nullptr) {
		m_visualEditor->Thaw();
		ibJournalWarning(wxT("designer"), wxT("form editor: the form was not drawn"));
		return;
	}
	Receive(m_formNode);

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

void ibVisualEditorNotebook::ibVisualEditor::Receive(const ibProtocolNode& node)
{
	ibValueForm* const valueForm = GetValueForm();
	const long long controlId = node.GetInt(ibProtocolName::ControlId);
	ibValueFrame* const control = controlId == valueForm->GetControlID() ? valueForm : valueForm->FindControlByID(controlId);

	if (control != nullptr) {
		// Built anew — its rows received again (the client's controls declare none).
		const auto found = m_rows.find(controlId);
		if (control->GetPropertyCount() == 0 && found != m_rows.end())
			control->Receive(found->second);

		// …and its values as its node has them now — what a command left; open in the tree too, the header's (SaveNode).
		Receive(control, node);
		control->SetExpanded(node.GetBool(ibProtocolName::Expanded, true));
	}

	for (const ibProtocolNode& child : node.Children())
		Receive(child);
}

void ibVisualEditorNotebook::ibVisualEditor::Receive(ibPropertyObject* object, const ibProtocolNode& node)
{
	// A property's, as WriteData wrote it; the wire brings a scalar back a field.
	ibProtocolNode values = node.Clone();
	values.Remove(ibProtocolName::NodeChildren);
	ibDataNode entries;
	ibReadProtocolNode(values, entries);
	for (unsigned int idx = 0; idx < object->GetPropertyCount(); idx++) {
		ibProperty* const prop = object->GetProperty(idx);
		const ibDataValue* value = entries.FindProperty(prop->GetName());
		if (value == nullptr)
			value = entries.FindField(prop->GetName());
		if (value != nullptr)
			prop->SetNodeValue(*value);
	}
}

bool ibVisualEditorNotebook::ibVisualEditor::SaveForm()
{
	// Nothing done since the form was handed back (or loaded) — nothing to hand.
	if (m_cmdProc.IsAtSavePoint() || !m_formNode.IsNode())
		return true;

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Form, m_formNode);
	m_document->Post(ibProtocolEvent::Change, args);
	m_cmdProc.SetSavePoint();
	return true;
}

ibVisualEditorNotebook::ibVisualEditor::~ibVisualEditor()
{
	// The inspector lets go of a control of this form before the controls go.
	ibValueFrame* const selected = GetSelectedObject();
	if (selected != nullptr && objectInspector->GetSelectedObject() == selected)
		objectInspector->SelectObject(nullptr);
}
