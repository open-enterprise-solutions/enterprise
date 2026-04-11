////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "treeDataProcessor.h"

#include "frontend/artProvider/artProvider.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include "docManager/templates/docViewDataProcessorFile.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorTree, wxPanel);

#define ICON_SIZE 16

ibDataProcessorTree::ibDataProcessorTree(ibMetaDocument* docParent, wxWindow* parent, wxWindowID id)
	: ibMetaDataTree(docParent, parent, id), m_metaData(nullptr), m_initialized(false)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizerCaption = new wxBoxSizer(wxVERTICAL);

	m_nameCaption = new wxStaticText(this, wxID_ANY, _("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_nameCaption->Wrap(-1);

	bSizerCaption->Add(m_nameCaption, 0, wxALL, 5);

	m_synonymCaption = new wxStaticText(this, wxID_ANY, _("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_synonymCaption->Wrap(-1);

	bSizerCaption->Add(m_synonymCaption, 0, wxALL, 5);

	m_commentCaption = new wxStaticText(this, wxID_ANY, _("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_commentCaption->Wrap(-1);

	bSizerCaption->Add(m_commentCaption, 0, wxALL, 5);

	m_defaultForm = new wxStaticText(this, wxID_ANY, _("Default form:"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultForm->Wrap(-1);

	bSizerCaption->Add(m_defaultForm, 0, wxALL, 5);
	bSizerHeader->Add(bSizerCaption, 0, wxEXPAND, 5);

	wxBoxSizer* bSizerValue = new wxBoxSizer(wxVERTICAL);

	m_nameValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_nameValue, 1, wxALL | wxEXPAND, 1);
	m_nameValue->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDataProcessorTree::OnEditCaptionName), nullptr, this);

	m_synonymValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_synonymValue, 1, wxALL | wxEXPAND, 1);
	m_synonymValue->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDataProcessorTree::OnEditCaptionSynonym), nullptr, this);

	m_commentValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_commentValue, 1, wxALL | wxEXPAND, 1);
	m_commentValue->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDataProcessorTree::OnEditCaptionComment), nullptr, this);

	m_defaultFormValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, NULL, 0);
	m_defaultFormValue->AppendString(_("<not selected>"));
	m_defaultFormValue->SetSelection(0);

	m_defaultFormValue->Connect(wxEVT_CHOICE, wxCommandEventHandler(ibDataProcessorTree::OnChoiceDefForm), nullptr, this);

	bSizerValue->Add(m_defaultFormValue, 1, wxALL | wxEXPAND, 1);
	bSizerHeader->Add(bSizerValue, 1, 0, 5);
	bSizerMain->Add(bSizerHeader, 0, wxEXPAND, 5);

	wxStaticBoxSizer* sbSizerTree = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("")), wxVERTICAL);

	m_metaTreeToolbar = new wxAuiToolBar(sbSizerTree->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DELETE, _("Delete"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)), _("Delete item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_UP, wxART_FRONTEND, wxSize(16, 16)), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_DOWN, wxART_FRONTEND, wxSize(16, 16)), _("Down item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_SORT, wxART_FRONTEND, wxSize(16, 16)), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new wxAuiLunaToolBarArt());

	sbSizerTree->Add(m_metaTreeToolbar, 0, wxALL | wxEXPAND, 0);

	m_metaTreeCtrl = new ibDataProcessorTreeCtrl(sbSizerTree->GetStaticBox(), this);
	m_metaTreeCtrl->SetBackgroundColour(wxColour(250, 250, 250));

	//set image list
	m_metaTreeCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	sbSizerTree->Add(m_metaTreeCtrl, 1, wxALL | wxEXPAND, 0);

	bSizerMain->Add(sbSizerTree, 1, wxEXPAND, 5);

	ibMetaDataDataProcessor* metaData = ((ibDataProcessorFilibDocument*)docParent)->GetMetaData();
	ibValueMetaObjectDataProcessor* commonMeta = metaData->GetDataProcessor();
	ibValueMetaObjectModule *moduleMeta = commonMeta->GetModuleObject();

	m_buttonModule = new wxButton(this, wxID_ANY, _("Open module"));
	m_buttonModule->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibDataProcessorTree::OnButtonModuleClicked), nullptr, this);
	m_buttonModule->SetBitmap(moduleMeta->GetIcon());

	bSizerMain->Add(m_buttonModule, 0, wxALL);

	this->SetSizer(bSizerMain);
	this->Layout();

	this->Centre(wxBOTH);

	//Initialize tree
	InitTree();
}

ibDataProcessorTree::~ibDataProcessorTree()
{
	m_nameValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(ibDataProcessorTree::OnEditCaptionName), nullptr, this);
	m_synonymValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(ibDataProcessorTree::OnEditCaptionSynonym), nullptr, this);
	m_commentValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(ibDataProcessorTree::OnEditCaptionComment), nullptr, this);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCreateItem, m_metaTreeCtrl, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnEditItem, m_metaTreeCtrl, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRemoveItem, m_metaTreeCtrl, ID_METATREE_DELETE);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnUpItem, m_metaTreeCtrl, ID_METATREE_UP);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnDownItem, m_metaTreeCtrl, ID_METATREE_DOWM);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSortItem, m_metaTreeCtrl, ID_METATREE_SORT);

	m_buttonModule->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(ibDataProcessorTree::OnButtonModuleClicked), nullptr, this);
}

void ibDataProcessorTree::OnEditCaptionName(wxCommandEvent& event)
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

	ibValueMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	dataProcessor->SetName(systemName);
	dataProcessor->SetSynonym(synonym);

	m_synonymValue->SetValue(synonym);

	m_docParent->OnChangeFilename(true);

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataProcessorTree::OnEditCaptionSynonym(wxCommandEvent& event)
{
	ibValueMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	dataProcessor->SetSynonym(m_synonymValue->GetValue());

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataProcessorTree::OnEditCaptionComment(wxCommandEvent& event)
{
	ibValueMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	dataProcessor->SetComment(m_commentValue->GetValue());

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataProcessorTree::OnChoiceDefForm(wxCommandEvent& event)
{
	ibValueMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);

	const ibMetaID id = static_cast<ibMetaID>(reinterpret_cast<intptr_t>(event.GetClientData()));
	if (id > 0) {
		dataProcessor->SetDefFormObject(id);
	}
	else {
		dataProcessor->SetDefFormObject(wxNOT_FOUND);
	}

	if (m_initialized) {
		m_metaData->Modify(true);
	}
}

void ibDataProcessorTree::OnButtonModuleClicked(wxCommandEvent& event)
{
	ibValueMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	OpenFormMDI(dataProcessor->GetModuleObject());
}

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorTree::ibDataProcessorTreeCtrl, wxTreeCtrl);

//**********************************************************************************
//*                                  metaTree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(ibDataProcessorTree::ibDataProcessorTreeCtrl, wxTreeCtrl)

EVT_LEFT_UP(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnLeftUp)
EVT_LEFT_DOWN(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnLeftDown)
EVT_LEFT_DCLICK(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnLeftDClick)
EVT_RIGHT_UP(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRightUp)
EVT_RIGHT_DOWN(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRightDown)
EVT_RIGHT_DCLICK(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRightDClick)
EVT_MOTION(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnMouseMove)
EVT_KEY_UP(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnKeyUp)
EVT_KEY_DOWN(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnExpanding)

EVT_MENU(ID_METATREE_NEW, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnEditItem)
EVT_MENU(ID_METATREE_DELETE, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnPropertyItem)

EVT_SET_FOCUS(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSetFocus)
EVT_KILL_FOCUS(ibDataProcessorTree::ibDataProcessorTreeCtrl::OnSetFocus)

EVT_MENU(wxID_COPY, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnCopyItem)
EVT_MENU(wxID_PASTE, ibDataProcessorTree::ibDataProcessorTreeCtrl::OnPasteItem)

wxEND_EVENT_TABLE()

ibDataProcessorTree::ibDataProcessorTreeCtrl::ibDataProcessorTreeCtrl()
	: wxTreeCtrl(), m_ownerTree(nullptr), m_metaView(new ibMetaView)
{
	//set double buffered
	SetDoubleBuffered(true);
}

ibDataProcessorTree::ibDataProcessorTreeCtrl::ibDataProcessorTreeCtrl(wxWindow* parentWnd, ibDataProcessorTree* ownerWnd)
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

#include "frontend/docView/docManager.h"

ibDataProcessorTree::ibDataProcessorTreeCtrl::~ibDataProcessorTreeCtrl()
{
	if (docManager != nullptr &&
		m_metaView == docManager->GetAnyUsableView()) {
		docManager->ActivateView(nullptr);
	}
	
	wxDELETE(m_metaView);
}