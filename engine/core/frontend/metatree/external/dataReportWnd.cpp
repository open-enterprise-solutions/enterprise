////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "dataReportWnd.h"
#include "compiler/debugger/debugClient.h"
#include "frontend/theme/luna_auitoolbar.h"

wxIMPLEMENT_DYNAMIC_CLASS(CDataReportTree, wxPanel);

extern wxImageList* GetImageList();

wxBEGIN_EVENT_TABLE(CDataReportTree, wxPanel)
wxEND_EVENT_TABLE()

CDataReportTree::CDataReportTree(CDocument* docParent, wxWindow* parent, wxWindowID id)
	: wxPanel(parent, id), m_docParent(docParent), m_initialize(false)
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
	m_nameValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionName), NULL, this);

	m_synonymValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_synonymValue, 1, wxALL | wxEXPAND, 1);
	m_synonymValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionSynonym), NULL, this);

	m_commentValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_commentValue, 1, wxALL | wxEXPAND, 1);
	m_commentValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionComment), NULL, this);

	m_defaultFormValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, NULL, 0);
	m_defaultFormValue->AppendString(_("<not selected>"));
	m_defaultFormValue->SetSelection(0);

	m_defaultFormValue->Connect(wxEVT_CHOICE, wxCommandEventHandler(CDataReportTree::OnChoiceDefForm), NULL, this);


	bSizerValue->Add(m_defaultFormValue, 1, wxALL | wxEXPAND, 1);
	bSizerHeader->Add(bSizerValue, 1, 0, 5);
	bSizerMain->Add(bSizerHeader, 0, wxEXPAND, 5);

	wxStaticBoxSizer* sbSizerTree = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("")), wxVERTICAL);

	m_metaTreeToolbar = new wxAuiToolBar(sbSizerTree->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_metaTreeToolbar->AddTool(ID_METATREE_NEW, _("new"), wxGetImageBMPFromResource(IDB_EDIT_NEW), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("new item"), _("new item"), NULL);
	m_metaTreeToolbar->AddTool(ID_METATREE_EDIT, _("edit"), wxGetImageBMPFromResource(IDB_EDIT), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("edit item"), _("edit item"), NULL);
	m_metaTreeToolbar->AddTool(ID_METATREE_REMOVE, _("remove"), wxGetImageBMPFromResource(IDB_EDIT_CUT), wxNullBitmap, wxItemKind::wxITEM_NORMAL, _("remove item"), _("remove item"), NULL);
	m_metaTreeToolbar->Realize();

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());

	sbSizerTree->Add(m_metaTreeToolbar, 0, wxALL | wxEXPAND, 0);

	m_metaTreeWnd = new CDataReportTreeWnd(sbSizerTree->GetStaticBox(), this);
	m_metaTreeWnd->SetBackgroundColour(RGB(250, 250, 250));

	//set image list
	m_metaTreeWnd->SetImageList(::GetImageList());

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	sbSizerTree->Add(m_metaTreeWnd, 1, wxALL | wxEXPAND, 0);

	bSizerMain->Add(sbSizerTree, 1, wxEXPAND, 5);

	m_buttonModule = new wxButton(this, wxID_ANY, _("open module"));
	m_buttonModule->Connect(wxEVT_BUTTON, wxCommandEventHandler(CDataReportTree::OnButtonModuleClicked), NULL, this);

	bSizerMain->Add(m_buttonModule, 0, wxALL);

	this->SetSizer(bSizerMain);
	this->Layout();

	this->Centre(wxBOTH);

	//Initialize tree
	InitTree();
}

CDataReportTree::~CDataReportTree()
{
	m_nameValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionName), NULL, this);
	m_synonymValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionSynonym), NULL, this);
	m_commentValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataReportTree::OnEditCaptionComment), NULL, this);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataReportTree::CDataReportTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_buttonModule->Connect(wxEVT_BUTTON, wxCommandEventHandler(CDataReportTree::OnButtonModuleClicked), NULL, this);
}

#include "utils/stringUtils.h"

void CDataReportTree::OnEditCaptionName(wxCommandEvent& event)
{
	wxString systemName = m_nameValue->GetValue();

	int pos = StringUtils::CheckCorrectName(systemName);
	if (pos > 0) {
		systemName = systemName.Left(pos);
		m_nameValue->SetValue(systemName);
		return;
	}

	wxString synonym = StringUtils::GenerateSynonym(systemName);

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
		report->m_defaultFormObject = id;
	}
	else {
		report->m_defaultFormObject = wxNOT_FOUND;
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

EVT_PROPERTY_MODIFIED(CDataReportTree::CDataReportTreeWnd::OnPropertyModified)

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

EVT_DEBUG(CDataReportTree::CDataReportTreeWnd::OnDebugEvent)

wxEND_EVENT_TABLE()

CDataReportTree::CDataReportTreeWnd::CDataReportTreeWnd()
	: wxTreeCtrl(), m_ownerTree(NULL)
{
	//set double buffered
	SetDoubleBuffered(true);
}

CDataReportTree::CDataReportTreeWnd::CDataReportTreeWnd(wxWindow* parentWnd, CDataReportTree* ownerWnd)
	: wxTreeCtrl(parentWnd, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT), m_ownerTree(ownerWnd)
{
	debugClient->AddHandler(this);

	//set double buffered
	SetDoubleBuffered(true);
}

CDataReportTree::CDataReportTreeWnd::~CDataReportTreeWnd()
{
	debugClient->RemoveHandler(this);
}