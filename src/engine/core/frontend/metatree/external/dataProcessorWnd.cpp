////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataprocessor window
////////////////////////////////////////////////////////////////////////////

#include "dataProcessorWnd.h"
#include "core/compiler/debugger/debugClient.h"
#include "frontend/theme/luna_auitoolbar.h"

#include "core/frontend/docView/templates/dataProcessorFile.h"

#include <wx/artprov.h>

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorTree, wxPanel);

#define ICON_SIZE 16

CDataProcessorTree::CDataProcessorTree(CDocument* docParent, wxWindow* parent, wxWindowID id)
	: IMetadataTree(docParent, parent, id), m_metaData(NULL), m_initialize(false)
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
	m_nameValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataProcessorTree::OnEditCaptionName), NULL, this);

	m_synonymValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_synonymValue, 1, wxALL | wxEXPAND, 1);
	m_synonymValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataProcessorTree::OnEditCaptionSynonym), NULL, this);

	m_commentValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	bSizerValue->Add(m_commentValue, 1, wxALL | wxEXPAND, 1);
	m_commentValue->Connect(wxEVT_TEXT, wxCommandEventHandler(CDataProcessorTree::OnEditCaptionComment), NULL, this);

	m_defaultFormValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, NULL, 0);
	m_defaultFormValue->AppendString(_("<not selected>"));
	m_defaultFormValue->SetSelection(0);

	m_defaultFormValue->Connect(wxEVT_CHOICE, wxCommandEventHandler(CDataProcessorTree::OnChoiceDefForm), NULL, this);

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

	m_metaTreeToolbar->SetArtProvider(new CAuiGenericToolBarArt());

	sbSizerTree->Add(m_metaTreeToolbar, 0, wxALL | wxEXPAND, 0);

	m_metaTreeWnd = new CDataProcessorTreeWnd(sbSizerTree->GetStaticBox(), this);
	m_metaTreeWnd->SetBackgroundColour(RGB(250, 250, 250));

	//set image list
	m_metaTreeWnd->SetImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Bind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

	sbSizerTree->Add(m_metaTreeWnd, 1, wxALL | wxEXPAND, 0);

	bSizerMain->Add(sbSizerTree, 1, wxEXPAND, 5);

	CMetadataDataProcessor* metaData = ((CDataProcessorEditDocument*)docParent)->GetMetadata();
	CMetaObjectDataProcessor* commonMeta = metaData->GetDataProcessor();
	CMetaModuleObject *moduleMeta = commonMeta->GetModuleObject();

	m_buttonModule = new wxButton(this, wxID_ANY, _("Open module"));
	m_buttonModule->Connect(wxEVT_BUTTON, wxCommandEventHandler(CDataProcessorTree::OnButtonModuleClicked), NULL, this);
	m_buttonModule->SetBitmap(moduleMeta->GetIcon());

	bSizerMain->Add(m_buttonModule, 0, wxALL);

	this->SetSizer(bSizerMain);
	this->Layout();

	this->Centre(wxBOTH);

	//Initialize tree
	InitTree();
}

CDataProcessorTree::~CDataProcessorTree()
{
	m_nameValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataProcessorTree::OnEditCaptionName), NULL, this);
	m_synonymValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataProcessorTree::OnEditCaptionSynonym), NULL, this);
	m_commentValue->Disconnect(wxEVT_TEXT, wxCommandEventHandler(CDataProcessorTree::OnEditCaptionComment), NULL, this);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnCreateItem, m_metaTreeWnd, ID_METATREE_NEW);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnEditItem, m_metaTreeWnd, ID_METATREE_EDIT);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnRemoveItem, m_metaTreeWnd, ID_METATREE_REMOVE);

	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnUpItem, m_metaTreeWnd, ID_METATREE_UP);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnDownItem, m_metaTreeWnd, ID_METATREE_DOWM);
	m_metaTreeToolbar->Unbind(wxEVT_MENU, &CDataProcessorTree::CDataProcessorTreeWnd::OnSortItem, m_metaTreeWnd, ID_METATREE_SORT);

	m_buttonModule->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(CDataProcessorTree::OnButtonModuleClicked), NULL, this);
}

#include "utils/stringUtils.h"

void CDataProcessorTree::OnEditCaptionName(wxCommandEvent& event)
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

	CMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	dataProcessor->SetName(systemName);
	dataProcessor->SetSynonym(synonym);

	m_synonymValue->SetValue(synonym);

	m_docParent->OnChangeFilename(true);

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataProcessorTree::OnEditCaptionSynonym(wxCommandEvent& event)
{
	CMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	dataProcessor->SetSynonym(m_synonymValue->GetValue());

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataProcessorTree::OnEditCaptionComment(wxCommandEvent& event)
{
	CMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	dataProcessor->SetComment(m_commentValue->GetValue());

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataProcessorTree::OnChoiceDefForm(wxCommandEvent& event)
{
	CMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);

	const meta_identifier_t id = reinterpret_cast<meta_identifier_t>(event.GetClientData());
	if (id > 0) {
		dataProcessor->SetDefFormObject(id);
	}
	else {
		dataProcessor->SetDefFormObject(wxNOT_FOUND);
	}

	if (m_initialize) {
		m_metaData->Modify(true);
	}
}

void CDataProcessorTree::OnButtonModuleClicked(wxCommandEvent& event)
{
	CMetaObjectDataProcessor* dataProcessor = m_metaData->GetDataProcessor();
	wxASSERT(dataProcessor);
	OpenFormMDI(dataProcessor->GetModuleObject());
}

wxIMPLEMENT_DYNAMIC_CLASS(CDataProcessorTree::CDataProcessorTreeWnd, wxTreeCtrl);

//**********************************************************************************
//*                                  metatree window						       *
//**********************************************************************************

wxBEGIN_EVENT_TABLE(CDataProcessorTree::CDataProcessorTreeWnd, wxTreeCtrl)

EVT_LEFT_UP(CDataProcessorTree::CDataProcessorTreeWnd::OnLeftUp)
EVT_LEFT_DOWN(CDataProcessorTree::CDataProcessorTreeWnd::OnLeftDown)
EVT_LEFT_DCLICK(CDataProcessorTree::CDataProcessorTreeWnd::OnLeftDClick)
EVT_RIGHT_UP(CDataProcessorTree::CDataProcessorTreeWnd::OnRightUp)
EVT_RIGHT_DOWN(CDataProcessorTree::CDataProcessorTreeWnd::OnRightDown)
EVT_RIGHT_DCLICK(CDataProcessorTree::CDataProcessorTreeWnd::OnRightDClick)
EVT_MOTION(CDataProcessorTree::CDataProcessorTreeWnd::OnMouseMove)
EVT_KEY_UP(CDataProcessorTree::CDataProcessorTreeWnd::OnKeyUp)
EVT_KEY_DOWN(CDataProcessorTree::CDataProcessorTreeWnd::OnKeyDown)

EVT_TREE_SEL_CHANGING(wxID_ANY, CDataProcessorTree::CDataProcessorTreeWnd::OnSelecting)
EVT_TREE_SEL_CHANGED(wxID_ANY, CDataProcessorTree::CDataProcessorTreeWnd::OnSelected)

EVT_TREE_ITEM_COLLAPSING(wxID_ANY, CDataProcessorTree::CDataProcessorTreeWnd::OnCollapsing)
EVT_TREE_ITEM_EXPANDING(wxID_ANY, CDataProcessorTree::CDataProcessorTreeWnd::OnExpanding)

EVT_MENU(ID_METATREE_NEW, CDataProcessorTree::CDataProcessorTreeWnd::OnCreateItem)
EVT_MENU(ID_METATREE_EDIT, CDataProcessorTree::CDataProcessorTreeWnd::OnEditItem)
EVT_MENU(ID_METATREE_REMOVE, CDataProcessorTree::CDataProcessorTreeWnd::OnRemoveItem)
EVT_MENU(ID_METATREE_PROPERTY, CDataProcessorTree::CDataProcessorTreeWnd::OnPropertyItem)

EVT_MENU(wxID_COPY, CDataProcessorTree::CDataProcessorTreeWnd::OnCopyItem)
EVT_MENU(wxID_PASTE, CDataProcessorTree::CDataProcessorTreeWnd::OnPasteItem)

EVT_DEBUG(CDataProcessorTree::CDataProcessorTreeWnd::OnDebugEvent)

wxEND_EVENT_TABLE()

CDataProcessorTree::CDataProcessorTreeWnd::CDataProcessorTreeWnd()
	: wxTreeCtrl(), m_ownerTree(NULL)
{
	//set double buffered
	SetDoubleBuffered(true);
}

CDataProcessorTree::CDataProcessorTreeWnd::CDataProcessorTreeWnd(wxWindow* parentWnd, CDataProcessorTree* ownerWnd)
	: wxTreeCtrl(parentWnd, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT), m_ownerTree(ownerWnd)
{
	debugClient->AddHandler(this);

	wxAcceleratorEntry entries[2];
	entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
	entries[1].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);

	wxAcceleratorTable accel(2, entries);
	SetAcceleratorTable(accel);

	//set double buffered
	SetDoubleBuffered(true);
}

CDataProcessorTree::CDataProcessorTreeWnd::~CDataProcessorTreeWnd()
{
	debugClient->RemoveHandler(this);
}