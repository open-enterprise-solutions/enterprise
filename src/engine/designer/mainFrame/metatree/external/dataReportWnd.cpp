////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "dataReportWnd.h"
#include "backend/debugger/debugClient.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include "docManager/templates/dataReportFile.h"

#include <wx/artprov.h>

wxIMPLEMENT_DYNAMIC_CLASS(CDataReportTree, wxPanel);

#define ICON_SIZE 16

CDataReportTree::CDataReportTree(CMetaDocument* docParent, wxWindow* parent, wxWindowID id)
	: IMetaDataTree(docParent, parent, id), m_metaData(nullptr), m_initialize(false)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizerCaption = new wxBoxSizer(wxVERTICAL);

	m_nameCaption = new wxStaticText(this, wxID_ANY, wxT("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_nameCaption->Wrap(-1);

	bSizerCaption->Add(m_nameCaption, 0, wxALL, 5);

	m_synonymCaption = new wxStaticText(this, wxID_ANY, wxT("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_synonymCaption->Wrap(-1);

	bSizerCaption->Add(m_synonymCaption, 0, wxALL, 5);

	m_commentCaption = new wxStaticText(this, wxID_ANY, wxT("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_commentCaption->Wrap(-1);

	bSizerCaption->Add(m_commentCaption, 0, wxALL, 5);

	m_defaultForm = new wxStaticText(this, wxID_ANY, wxT("Default form:"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultForm->Wrap(-1);

	bSizerCaption->Add(m_defaultForm, 0, wxALL, 5);
	bSizerHeader->Add(bSizerCaption, 0, wxEXPAND, 5);

	wxBoxSizer* bSizerValue = new wxBoxSizer(wxVERTICAL);

	m_nameValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_nameValue, 1, wxALL | wxEXPAND, 1);
	m_nameValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionName), nullptr, this);

	m_synonymValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_synonymValue, 1, wxALL | wxEXPAND, 1);
	m_synonymValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionSynonym), nullptr, this);

	m_commentValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_commentValue, 1, wxALL | wxEXPAND, 1);
	m_commentValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionComment), nullptr, this);

	m_defaultFormValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, NULL, 0);
	m_defaultFormValue->AppendString(_("<not selected>"));
	m_defaultFormValue->SetSelection(0);

	m_defaultFormValue->Connect(wxEVT_CHOICE, wxCommandEventHandler(CDataReportTree::OnChoiceDefForm), nullptr, this);

	bSizerValue->Add(m_defaultFormValue, 1, wxALL | wxEXPAND, 1);
	bSizerHeader->Add(bSizerValue, 1, 0, 5);
	bSizerMain->Add(bSizerHeader, 0, wxEXPAND, 5);

	wxStaticBoxSizer* sbSizerTree = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("")), wxVERTICAL);

	m_metaTreeToolbar = new wxAuiToolBar(sbSizerTree->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("New"), wxArtProvider::GetBitmap(wxART_PLUS, wxART_MENU), _("New item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("Edit"), wxArtProvider::GetBitmap(wxART_EDIT, wxART_MENU), _("Edit item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("remove"), wxArtProvider::GetBitmap(wxART_DELETE, wxART_MENU), _("Remove item"));
	m_metaTreeToolbar->AddSeparator();
	m_metaTreeToolbar->AddTool(ID_METATREE_UP, _("Up"), wxArtProvider::GetBitmap(wxART_GO_UP, wxART_MENU), _("Up item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_DOWM, _("Down"), wxArtProvider::GetBitmap(wxART_GO_DOWN, wxART_MENU), _("Down item"));
	m_metaTreeToolbar->AddTool(ID_METATREE_SORT, _("Sort"), wxArtProvider::GetBitmap(wxART_HELP_SETTINGS, wxART_MENU), _("Sort item"));
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new wxAuiLunaToolBarArt());

	sbSizerTree->Add(m_metaTreeToolbar, 0, wxALL | wxEXPAND, 0);

	m_metaTreeWnd = new CDataReportTreeWnd(sbSizerTree->GetStaticBox(), this);
	m_metaTreeWnd->SetBackgroundColour(RGB(250, 250, 250));

	//set image list
	m_metaTreeWnd->SetImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

	sbSizerTree->Add(m_metaTreeWnd, 1, wxALL | wxEXPAND, 0);

	bSizerMain->Add(sbSizerTree, 1, wxEXPAND, 5);

	CMetaDataReport* metaData = ((CReportEditDocument*)docParent)->GetMetaData();
	CMetaObjectReport* commonMeta = metaData->GetReport();
	CMetaObjectModule* moduleMeta = commonMeta->GetModuleObject();

	m_buttonModule = new wxButton(this, wxID_ANY, _("Open module"));
	m_buttonModule->Connect(wxEVT_BUTTON, wxCommandEventHandler(CDataReportTree::OnButtonModuleClicked), nullptr, this);
	m_buttonModule->SetBitmap(moduleMeta->GetIcon());

	bSizerMain->Add(m_buttonModule, 0, wxALL);

	this->SetSizer(bSizerMain);
	this->Layout();

	this->Centre(wxBOTH);

	//Initialize tree
	InitTree();
}

CDataReportTree::~CDataReportTree()
{
	m_nameValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionName), nullptr, this);
	m_synonymValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionSynonym), nullptr, this);
	m_commentValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionComment), nullptr, this);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

	m_buttonModule->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(CDataReportTree::OnButtonModuleClicked), nullptr, this);
}



void CDataReportTree::OnEditCaptionName(wxCommandEvent& event)
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

	CMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->SetName(systemName);
	report->SetSynonym(synonym);

	m_synonymValue->SetValue(synonym);

	m_docParent->OnChangeFilename(true);

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataReportTree::OnEditCaptionSynonym(wxCommandEvent& event)
{
	CMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->SetSynonym(m_synonymValue->GetValue());

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataReportTree::OnEditCaptionComment(wxCommandEvent& event)
{
	CMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	report->SetComment(m_commentValue->GetValue());

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataReportTree::OnChoiceDefForm(wxCommandEvent& event)
{
	CMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);

	const meta_identifier_t id = reinterpret_cast<meta_identifier_t>(event.GetClientData());
	if (id > 0) {
		report->SetDefFormObject(id);
	}
	else {
		report->SetDefFormObject(wxNOT_FOUND);
	}

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataReportTree::OnButtonModuleClicked(wxCommandEvent& event)
{
	CMetaObjectReport* report = m_metaData->GetReport();
	wxASSERT(report);
	OpenFormMDI(report->GetModuleObject());
}

wxIMPLEMENT_DYNAMIC_CLASS(CDataReportTree::CDataReportTreeWnd, wxTreeCtrl);

//**********************************************************************************
//*                                  metatree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(CDataReportTree::CDataReportTreeWnd, wxTreeCtrl)

EVT_LEFT_UP(CDataReportTree::CDataReportTreeWnd::OnLeftUp)
EVT_LEFT_DOWN(CDataReportTree::CDataReportTreeWnd::OnLeftDown)
EVT_LEFT_DCLICK(CDataReportTree::CDataReportTreeWnd::OnLeftDClick)
EVT_RIGHT_UP(CDataReportTree::CDataReportTreeWnd::OnRightUp)
EVT_RIGHT_DOWN(CDataReportTree::CDataReportTreeWnd::OnRightDown)
EVT_RIGHT_DCLICK(CDataReportTree::CDataReportTreeWnd::OnRightDClick)
EVT_MOTION(CDataReportTree::CDataReportTreeWnd::OnMouseMove)
EVT_KEY_UP(CDataReportTree::CDataReportTreeWnd::OnKeyUp)
EVT_KEY_DOWN(CDataReportTree::CDataReportTreeWnd::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, CDataReportTree::CDataReportTreeWnd::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, CDataReportTree::CDataReportTreeWnd::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, CDataReportTree::CDataReportTreeWnd::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CDataReportTree::CDataReportTreeWnd::OnExpanding)

EVT_MENU(ID_METATREE_NEW, CDataReportTree::CDataReportTreeWnd::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, CDataReportTree::CDataReportTreeWnd::OnEditItem)
EVT_MENU(ID_METATREE_REMOVE, CDataReportTree::CDataReportTreeWnd::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, CDataReportTree::CDataReportTreeWnd::OnPropertyItem)

EVT_MENU(wxID_COPY, CDataReportTree::CDataReportTreeWnd::OnCopyItem)
EVT_MENU(wxID_PASTE, CDataReportTree::CDataReportTreeWnd::OnPasteItem)

//EVT_DEBUG(CDataReportTree::CDataReportTreeWnd::OnDebugEvent)

wxEND_EVENT_TABLE()

CDataReportTree::CDataReportTreeWnd::CDataReportTreeWnd()
	: wxTreeCtrl(), m_ownerTree(nullptr)
{
	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

CDataReportTree::CDataReportTreeWnd::CDataReportTreeWnd(wxWindow* parentWnd, CDataReportTree* ownerWnd)
	: wxTreeCtrl(parentWnd, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_TWIST_BUTTONS), m_ownerTree(ownerWnd)
{
	//debugClient->AddHandler(this);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

CDataReportTree::CDataReportTreeWnd::~CDataReportTreeWnd()
{
	//debugClient->RemoveHandler(this);
}