#include "toolbar.h"
#include "frontend/visualView/visualHostClient.h"
#include "frontend/win/theme/luna_toolbarart.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueToolbar, ibValueWindow);

//***********************************************************************************
//*                                  Custom Aui toolbar                             *
//***********************************************************************************

void ibValueToolbar::AddToolItem()
{
	wxASSERT(m_formOwner);
	ibValueFrame* toolItem = m_formOwner->NewObject(g_controlToolBarItemCLSID, this);
	g_visualHostContext->InsertControl(toolItem, this);
	g_visualHostContext->RefreshEditor();
}

void ibValueToolbar::AddToolSeparator()
{
	wxASSERT(m_formOwner);
	ibValueFrame* toolSeparator = m_formOwner->NewObject(g_controlToolBarSeparatorCLSID, this);
	g_visualHostContext->InsertControl(toolSeparator, this);
	g_visualHostContext->RefreshEditor();
}

//***********************************************************************************
//*                                 Value Toolbar                                   *
//***********************************************************************************

ibValueToolbar::ibValueToolbar() : ibValueWindow()
{
}

wxObject* ibValueToolbar::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = new ibAuiToolBar(wxparent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_NO_AUTORESIZE | wxAUI_TB_HORZ_TEXT | wxAUI_TB_OVERFLOW);
	toolbar->SetArtProvider(new wxAuiLunaToolBarArt());
	toolbar->Bind(wxEVT_TOOL, &ibValueToolbar::OnTool, this);
	toolbar->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &ibValueToolbar::OnToolDropDown, this);
	toolbar->Bind(wxEVT_LEFT_DOWN, &ibValueToolbar::OnToolBarLeftDown, this);

	return toolbar;
}

void ibValueToolbar::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& firstСreated) {
		ibValueToolbar::AddToolItem();
	}
}

void ibValueToolbar::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(wxobject);

	if (toolbar != nullptr) {

		const ibActionID id = GetActionSrc(); 
		ibValueFrame* sourceElement =
			id != wxNOT_FOUND ? FindControlByID(id) : nullptr;
	
		if (sourceElement != nullptr)
			m_actionArray = sourceElement->GetActionCollection(sourceElement->GetTypeForm());
		else
			m_actionArray = ibActionCollection();

	}

	UpdateWindow(toolbar);
}

void ibValueToolbar::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValueToolbar::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(obj);
	toolbar->Unbind(wxEVT_TOOL, &ibValueToolbar::OnTool, this);
}

//**********************************************************************************
//*										 Property                                  *
//**********************************************************************************

bool ibValueToolbar::LoadData(ibReaderMemory& reader)
{
	m_actSource->LoadData(reader);
	return ibValueWindow::LoadData(reader);
}

bool ibValueToolbar::SaveData(ibWriterMemory writer)
{
	m_actSource->SaveData(writer);
	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueToolbar, "Toolbar", "Toolbar", g_controlToolBarCLSID);