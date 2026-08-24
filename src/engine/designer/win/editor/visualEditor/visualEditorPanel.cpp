////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : visual editor 
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "titleFrame.h"
#include "frontend/visualView/ctrl/form.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibVisualEditorNotebook::ibVisualEditor, wxPanel);

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditor() :
	wxPanel(), m_visualEditor(nullptr)
{
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditor(ibMetaDocument* document, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_document(document), m_visualEditor(nullptr), m_cmdProc(new ibCommandProcessor()), m_valueForm(nullptr)
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

	m_visualEditor = new ibVisualEditorHost(this, m_splitter);

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

#include "frontend/docView/docView.h" 
#include "frontend/mainFrame/mainFrame.h"

void ibVisualEditorNotebook::ibVisualEditor::ActivateEditor()
{
	// Restore the CURRENT element (control / attribute / command — whichever was selected last), NOT always the
	// canvas control, so re-activating the editor keeps your place instead of snapping the inspector back onto a
	// control. No SetFocus: activation must not YANK focus onto the form canvas — that stole focus from the tree
	// you were in (and from an unrelated surface, e.g. including a command in a section, that merely refreshed
	// this doc). The notebook page change already gives the editor focus when you genuinely switch to it.
	// PASSIVE restore — just re-light the inspector on the current element (or the canvas control if none), through the
	// selection's own Reveal. Soft (no forceOpen): re-activating never pops the inspector open. No SetCurrentElement —
	// this must not re-highlight the canvas or notify; it only re-shows what was already selected.
	ibPropertyObject* element = GetCurrentElement() != nullptr ? GetCurrentElement() : GetSelectedObject();
	ibEditorSelection(element).Reveal(false);
}

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataConfiguration.h"

bool ibVisualEditorNotebook::ibVisualEditor::LoadForm()
{
	if (m_document == nullptr)
		return false;

	const ibValueMetaObjectFormBase* creator = m_document->GetMetaObject()->ConvertToType<ibValueMetaObjectFormBase>();

	if (creator == nullptr)
		return false;

	const ibMetaData* metaData = creator->GetMetaData();
	wxASSERT(metaData);

	auto* cc = metaData->GetCompileCache();
	if (!cc || !cc->FindCompileModule(creator, m_valueForm)) {
		m_valueForm = new ibValueForm(creator, nullptr);
		if (!creator->LoadFormData(m_valueForm)) {
			wxDELETE(m_valueForm);
			ibJournalWarning(wxT("designer"), _("Form editor: the form \"%s\" could not be loaded (its stored data is missing or unreadable)."),
				creator->GetName().c_str());
			return false;
		}
	}

	// Do not proceed with a null form — the visual host / AUI teardown asserts on a half-created view.
	// Warn instead of silently failing so the user knows the editor could not open the form.
	if (m_valueForm == nullptr) {
		ibJournalWarning(wxT("designer"), _("Form editor: the form \"%s\" could not be opened."), creator->GetName().c_str());
		return false;
	}

	m_valueForm->IncrRef();
	m_visualEditor->Freeze();
	
	NotifyEditorLoaded();

	// first create control 
	m_visualEditor->CreateVisualHost();
		
	//refresh project
	NotifyEditorRefresh();

	m_visualEditor->Thaw();
	return true;
}

bool ibVisualEditorNotebook::ibVisualEditor::SaveForm()
{
	ibValueMetaObjectFormBase* creator = m_document->ConvertMetaObjectToType<ibValueMetaObjectFormBase>();

	// Create a std::string and copy your document data in to the string
	if (creator != nullptr) {
		creator->SaveFormData(m_valueForm);
	}

	NotifyEditorSaved();
	return true;
}

void ibVisualEditorNotebook::ibVisualEditor::TestForm()
{
	// Down to the doc/view side explicitly: ibMetaDocument IS-A ibBackendMetaDocument AND an
	// ibDocument, so both ShowForm overloads would match it equally well.
	m_valueForm->ShowForm(static_cast<ibDocument*>(m_document), false);
}

ibVisualEditorNotebook::ibVisualEditor::~ibVisualEditor()
{
	m_visualEditor->Destroy();
	m_objectTree->Destroy();
	m_splitter->Destroy();

	if (m_valueForm != nullptr) 
		m_valueForm->DecrRef();
	
	wxDELETE(m_cmdProc);
}