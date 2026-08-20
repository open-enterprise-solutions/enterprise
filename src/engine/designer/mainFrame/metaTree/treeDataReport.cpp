////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"

#include "frontend/artProvider/artProvider.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include "docManager/templates/docViewDataReportFile.h"

// ITS REAL BASE. This said wxPanel — true only two steps up, so wx's own class chain did not know
// this tree IS a metadata tree, and a wxDynamicCast to the base would have come back null. Nothing
// asks that today; the configuration tree has always declared its base correctly.
wxIMPLEMENT_DYNAMIC_CLASS(ibDataReportTree, ibMetaTreeBase);

#define ICON_SIZE 16

ibDataReportTree::ibDataReportTree(ibMetaDocument* docParent, wxWindow* parent, wxWindowID id)
	: ibMetaTreeBase(docParent, parent, id), m_metaData(nullptr), m_initialized(false)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizerCaption = new wxBoxSizer(wxVERTICAL);

	m_nameCaption = new wxStaticText(this, wxID_ANY, _("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_nameCaption->Wrap(-1);

	bSizerCaption->Add(m_nameCaption, 0, wxALL, FromDIP(5));

	m_synonymCaption = new wxStaticText(this, wxID_ANY, _("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_synonymCaption->Wrap(-1);

	bSizerCaption->Add(m_synonymCaption, 0, wxALL, FromDIP(5));

	m_commentCaption = new wxStaticText(this, wxID_ANY, _("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_commentCaption->Wrap(-1);

	bSizerCaption->Add(m_commentCaption, 0, wxALL, FromDIP(5));

	m_defaultForm = new wxStaticText(this, wxID_ANY, _("Default form:"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultForm->Wrap(-1);

	bSizerCaption->Add(m_defaultForm, 0, wxALL, FromDIP(5));

	// ⭐ THE REPORT'S OTHER DECLARATION, beside its default form: WHICH COMPOSER IS THE MAIN ONE.
	// That is what a generated form is built from, so a person opening an external report needs to
	// see it — and to change it — exactly where they see the default form (Max, 2026-08-20).
	m_defaultComposer = new wxStaticText(this, wxID_ANY, _("Default composer:"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultComposer->Wrap(-1);

	bSizerCaption->Add(m_defaultComposer, 0, wxALL, FromDIP(5));
	bSizerHeader->Add(bSizerCaption, 0, wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizerValue = new wxBoxSizer(wxVERTICAL);

	m_nameValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_nameValue, 1, wxALL | wxEXPAND, 1);
	m_nameValue->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDataReportTree::OnEditCaptionName), nullptr, this);

	m_synonymValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_synonymValue, 1, wxALL | wxEXPAND, 1);
	m_synonymValue->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDataReportTree::OnEditCaptionSynonym), nullptr, this);

	m_commentValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_commentValue, 1, wxALL | wxEXPAND, 1);
	m_commentValue->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDataReportTree::OnEditCaptionComment), nullptr, this);

	m_defaultFormValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, NULL, 0);
	m_defaultFormValue->AppendString(_("<not selected>"));
	m_defaultFormValue->SetSelection(0);

	m_defaultFormValue->Connect(wxEVT_CHOICE, wxCommandEventHandler(ibDataReportTree::OnChoiceDefForm), nullptr, this);

	bSizerValue->Add(m_defaultFormValue, 1, wxALL | wxEXPAND, 1);

	m_defaultComposerValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, NULL, 0);
	m_defaultComposerValue->AppendString(_("<not selected>"));
	m_defaultComposerValue->SetSelection(0);

	m_defaultComposerValue->Connect(wxEVT_CHOICE, wxCommandEventHandler(ibDataReportTree::OnChoiceDefComposer), nullptr, this);

	bSizerValue->Add(m_defaultComposerValue, 1, wxALL | wxEXPAND, 1);
	bSizerHeader->Add(bSizerValue, 1, 0, FromDIP(5));
	bSizerMain->Add(bSizerHeader, 0, wxEXPAND, FromDIP(5));

	wxStaticBoxSizer* sbSizerTree = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("")), wxVERTICAL);

	CreateToolBar(sbSizerTree->GetStaticBox());   // the base builds it — this was a fifth copy

	sbSizerTree->Add(m_metaTreeToolbar, 0, wxALL | wxEXPAND, 0);

	// Card-style depth — panel powder-blue, tree cream (matches editor).
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue panel
	m_metaTreeCtrl = new ibDataReportTreeCtrl(sbSizerTree->GetStaticBox(), this);
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 247, 240));  // #FAF7F0 cream tree

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	sbSizerTree->Add(m_metaTreeCtrl, 1, wxALL | wxEXPAND, 0);

	bSizerMain->Add(sbSizerTree, 1, wxEXPAND, FromDIP(5));

	m_buttonModule = new wxButton(this, wxID_ANY, _("Open module"));
	m_buttonModule->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibDataReportTree::OnButtonModuleClicked), nullptr, this);

	// THE BUTTON'S PICTURE — every step asked rather than assumed; see the twin note in
	// treeDataProcessor.cpp for why the C-style downcast that stood here was worse than useless.
	if (const ibReportFileDocument* fileDoc = wxDynamicCast(docParent, ibReportFileDocument)) {
		if (ibMetaDataReport* metaData = fileDoc->GetMetaData()) {
			if (ibValueMetaObjectReport* commonMeta = metaData->GetReport()) {
				if (const ibValueMetaObjectModule* moduleMeta = commonMeta->GetObjectModule())
					m_buttonModule->SetBitmap(moduleMeta->GetIcon());
			}
		}
	}

	bSizerMain->Add(m_buttonModule, 0, wxALL, 0);

	this->SetSizer(bSizerMain);
	this->Layout();

	this->Centre(wxBOTH);

	//Initialize tree
	InitTree();
}

ibDataReportTree::~ibDataReportTree()
{
	// LET GO OF THE BACK-POINTER — see the twin note in treeDataProcessor.cpp.
	if (m_metaData != nullptr)
		m_metaData->SetMetaTree(nullptr);

	// ASK BEFORE TEARING DOWN. The default constructor is reachable through wxCreateDynamicObject
	// and leaves every control null; the configuration tree guards exactly this, the twins did not.
	if (m_nameValue == nullptr || m_metaTreeToolbar == nullptr || m_metaTreeCtrl == nullptr)
		return;

	m_nameValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(ibDataReportTree::OnEditCaptionName), nullptr, this);
	m_synonymValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(ibDataReportTree::OnEditCaptionSynonym), nullptr, this);
	m_commentValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(ibDataReportTree::OnEditCaptionComment), nullptr, this);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataReportTree::ibDataReportTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	m_buttonModule->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(ibDataReportTree::OnButtonModuleClicked), nullptr, this);
}

void ibDataReportTree::OnEditCaptionName(wxCommandEvent& event)
{
	wxString systemName = m_nameValue->GetValue();

	int pos = stringUtils::CheckCorrectName(systemName);
	if (pos > 0) {
		systemName = systemName.Left(pos);
		m_nameValue->SetValue(systemName);
		return;
	}

	wxString synonym = stringUtils::GenerateSynonym(systemName);

	m_docParent->SetFilename(systemName);
	m_docParent->SetTitle(systemName);

	ibValueMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->SetName(systemName);
	report->SetSynonym(synonym);

	m_synonymValue->SetValue(synonym);

	m_docParent->OnChangeFilename(true);

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataReportTree::OnEditCaptionSynonym(wxCommandEvent& event)
{
	ibValueMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->SetSynonym(m_synonymValue->GetValue());

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataReportTree::OnEditCaptionComment(wxCommandEvent& event)
{
	ibValueMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->SetComment(m_commentValue->GetValue());

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataReportTree::OnChoiceDefForm(wxCommandEvent& event)
{
	ibValueMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);

	const ibMetaID id = static_cast<ibMetaID>(reinterpret_cast<intptr_t>(event.GetClientData()));
	if (id > 0) {
		report->SetDefFormObject(id);
	}
	else {
		report->SetDefFormObject(wxNOT_FOUND);
	}

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

// The twin of OnChoiceDefForm: the row carries the composer's metaID as its client data, so the
// choice hands back an identity and nothing is matched by label.
void ibDataReportTree::OnChoiceDefComposer(wxCommandEvent& event)
{
	ibValueMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);

	const ibMetaID id = static_cast<ibMetaID>(reinterpret_cast<intptr_t>(event.GetClientData()));
	report->SetDefComposer(id > 0 ? id : wxNOT_FOUND);

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataReportTree::OnButtonModuleClicked(wxCommandEvent& event)
{
	ibValueMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->ProcessCommand(ibValueMetaObjectReport::ID_METATREE_OPEN_MODULE);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibDataReportTree::ibDataReportTreeCtrl, wxTreeCtrl);

//**********************************************************************************
//*                                  metaTree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(ibDataReportTree::ibDataReportTreeCtrl, wxTreeCtrl)

EVT_LEFT_UP(ibDataReportTree::ibDataReportTreeCtrl::OnLeftUp)
EVT_LEFT_DOWN(ibDataReportTree::ibDataReportTreeCtrl::OnLeftDown)
EVT_LEFT_DCLICK(ibDataReportTree::ibDataReportTreeCtrl::OnLeftDClick)
EVT_RIGHT_UP(ibDataReportTree::ibDataReportTreeCtrl::OnRightUp)
EVT_RIGHT_DOWN(ibDataReportTree::ibDataReportTreeCtrl::OnRightDown)
EVT_RIGHT_DCLICK(ibDataReportTree::ibDataReportTreeCtrl::OnRightDClick)
EVT_MOTION(ibDataReportTree::ibDataReportTreeCtrl::OnMouseMove)
EVT_KEY_UP(ibDataReportTree::ibDataReportTreeCtrl::OnKeyUp)
EVT_KEY_DOWN(ibDataReportTree::ibDataReportTreeCtrl::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, ibDataReportTree::ibDataReportTreeCtrl::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, ibDataReportTree::ibDataReportTreeCtrl::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, ibDataReportTree::ibDataReportTreeCtrl::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibDataReportTree::ibDataReportTreeCtrl::OnExpanding)

EVT_MENU(ID_METATREE_NEW, ibDataReportTree::ibDataReportTreeCtrl::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, ibDataReportTree::ibDataReportTreeCtrl::OnEditItem)
EVT_MENU(ID_METATREE_DELETE, ibDataReportTree::ibDataReportTreeCtrl::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, ibDataReportTree::ibDataReportTreeCtrl::OnPropertyItem)

EVT_SET_FOCUS(ibDataReportTree::ibDataReportTreeCtrl::OnSetFocus)
EVT_KILL_FOCUS(ibDataReportTree::ibDataReportTreeCtrl::OnSetFocus)

EVT_MENU(wxID_COPY, ibDataReportTree::ibDataReportTreeCtrl::OnCopyItem)
EVT_MENU(wxID_PASTE, ibDataReportTree::ibDataReportTreeCtrl::OnPasteItem)

wxEND_EVENT_TABLE()

ibDataReportTree::ibDataReportTreeCtrl::ibDataReportTreeCtrl()
	: wxTreeCtrl(), m_ownerTree(nullptr), m_metaView(new ibMetaView)
{
	// (the accelerator table is set in the real constructor below — it stood here as well, in this
	// copy only, which is the sort of thing that makes two files stop being the same file)

	//set double buffered
	SetDoubleBuffered(true);
}

ibDataReportTree::ibDataReportTreeCtrl::ibDataReportTreeCtrl(wxWindow* parentWnd, ibDataReportTree* ownerWnd)
	: wxTreeCtrl(parentWnd, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS), m_ownerTree(ownerWnd), m_metaView(new ibMetaView)
{
	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

#include "frontend/docView/docView.h"

ibDataReportTree::ibDataReportTreeCtrl::~ibDataReportTreeCtrl()
{
	if (docManager != nullptr &&
		m_metaView == docManager->GetAnyUsableView()) {
		docManager->ActivateView(nullptr);
	}

	wxDELETE(m_metaView);
}