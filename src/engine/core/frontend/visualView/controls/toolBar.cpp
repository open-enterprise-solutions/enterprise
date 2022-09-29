#include "toolbar.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/visualView/visualHost.h"
#include "frontend/theme/luna_auitoolbar.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueToolbar, IValueWindow);

//***********************************************************************************
//*                                  Custom Aui toolbar                             *
//***********************************************************************************

void CValueToolbar::AddToolItem()
{
	wxASSERT(m_formOwner);
	IValueFrame* toolItem = m_formOwner->NewObject("tool", this);
	g_visualHostContext->InsertObject(toolItem, this);
	g_visualHostContext->RefreshEditor();
}

void CValueToolbar::AddToolSeparator()
{
	wxASSERT(m_formOwner);
	IValueFrame* toolSeparator = m_formOwner->NewObject("toolSeparator", this);
	g_visualHostContext->InsertObject(toolSeparator, this);
	g_visualHostContext->RefreshEditor();
}

//***********************************************************************************
//*                                 Value Toolbar                                   *
//***********************************************************************************

CValueToolbar::CValueToolbar() : IValueWindow()
{
}

wxObject* CValueToolbar::Create(wxObject* parent, IVisualHost* visualHost)
{
	CAuiToolBar* toolbar = new CAuiToolBar((wxWindow*)parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	toolbar->SetArtProvider(new CAuiGenericToolBarArt());
	toolbar->Bind(wxEVT_TOOL, &CValueToolbar::OnTool, this);
	toolbar->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &CValueToolbar::OnToolDropDown, this);
	return toolbar;
}

void CValueToolbar::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& first—reated) {
		CValueToolbar::AddToolItem();
	}
}

void CValueToolbar::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	CAuiToolBar* toolbar = dynamic_cast<CAuiToolBar*>(wxobject);

	if (toolbar != NULL) {
	}

	UpdateWindow(toolbar);
}

void CValueToolbar::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
}

void CValueToolbar::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	CAuiToolBar* toolbar = dynamic_cast<CAuiToolBar*>(obj);
	toolbar->Unbind(wxEVT_TOOL, &CValueToolbar::OnTool, this);
}

//**********************************************************************************
//*										 Property                                  *
//**********************************************************************************

bool CValueToolbar::LoadData(CMemoryReader& reader)
{
	m_actSource->SetValue(reader.r_s32());
	return IValueWindow::LoadData(reader);
}

bool CValueToolbar::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_actSource->GetValueAsInteger());
	return IValueWindow::SaveData(writer);
}