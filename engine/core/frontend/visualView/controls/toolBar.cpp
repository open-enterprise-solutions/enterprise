#include "toolbar.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/visualView/visualEditorView.h"

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

	IValueControl *toolItem =
		m_formOwner->NewObject("tool", this);

	toolItem->ReadProperty();

	m_visualHostContext->InsertObject(toolItem, this);
	toolItem->SaveProperty();

	m_visualHostContext->RefreshEditor();
}

void CValueToolbar::AddToolSeparator()
{
	wxASSERT(m_formOwner);

	IValueControl *toolSeparator =
		m_formOwner->NewObject("toolSeparator", this);

	toolSeparator->ReadProperty();

	m_visualHostContext->InsertObject(toolSeparator, this);
	toolSeparator->SaveProperty();

	m_visualHostContext->RefreshEditor();
}

//***********************************************************************************
//*                                 Value Toolbar                                   *
//***********************************************************************************

CValueToolbar::CValueToolbar() : IValueWindow(),
m_bitmapsize(wxDefaultSize), m_margins(wxDefaultSize),
m_packing(1), m_separation(5), m_actionSource(wxNOT_FOUND)
{
	PropertyContainer *categoryToolbar = IObjectBase::CreatePropertyContainer("ToolBar");
	categoryToolbar->AddProperty("name", PropertyType::PT_WXNAME);
	/*categoryToolbar->AddProperty("bitmapsize", PropertyType::PT_WXSIZE);
	categoryToolbar->AddProperty("margins", PropertyType::PT_WXSIZE);
	categoryToolbar->AddProperty("packing", PropertyType::PT_INT);
	categoryToolbar->AddProperty("separation", PropertyType::PT_INT);*/

	m_category->AddCategory(categoryToolbar);

	PropertyContainer *categoryAction = IObjectBase::CreatePropertyContainer("Action");
	categoryAction->AddProperty("action_source", PropertyType::PT_OPTION, &CValueToolbar::GetActionSource);

	m_category->AddCategory(categoryAction);
}

wxObject* CValueToolbar::Create(wxObject* parent, IVisualHost *visualHost)
{
	CAuiToolBar *toolbar = new CAuiToolBar((wxWindow*)parent, wxID_ANY, m_pos, m_size, wxAUI_TB_HORZ_TEXT | wxAUI_TB_OVERFLOW);

	/*if (m_bitmapsize != wxDefaultSize) { toolbar->SetToolBitmapSize(m_bitmapsize); }
	if (m_margins != wxDefaultSize) { toolbar->SetMargins(m_margins.GetWidth(), m_margins.GetHeight()); }

	if (!IsNull(wxT("packing"))) toolbar->SetToolPacking(m_packing);
	if (!IsNull(wxT("separation"))) toolbar->SetToolSeparation(m_separation);*/

	toolbar->SetArtProvider(new wxAuiGenericToolBarArt());
	toolbar->Bind(wxEVT_TOOL, &CValueToolbar::OnTool, this);

	return toolbar;
}

void CValueToolbar::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	if (visualHost->IsDesignerHost() && GetChildCount() == 0
		&& first—reated) {
		CValueToolbar::AddToolItem();
	}
}

void CValueToolbar::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	CAuiToolBar *toolbar = dynamic_cast<CAuiToolBar *>(wxobject);

	if (toolbar)
	{
	}

	UpdateWindow(toolbar);
}

void CValueToolbar::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
}

void CValueToolbar::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
	CAuiToolBar *toolbar = dynamic_cast<CAuiToolBar *>(obj);
	toolbar->Unbind(wxEVT_TOOL, &CValueToolbar::OnTool, this);
}

//**********************************************************************************
//*										 Property                                  *
//**********************************************************************************

bool CValueToolbar::LoadData(CMemoryReader &reader)
{
	m_actionSource = reader.r_s32();
	return IValueWindow::LoadData(reader);
}

bool CValueToolbar::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_actionSource);
	return IValueWindow::SaveData(writer);
}

//**********************************************************************************
//*										 Property                                  *
//**********************************************************************************

void CValueToolbar::ReadProperty()
{
	IValueWindow::ReadProperty();

	IObjectBase::SetPropertyValue("name", m_controlName);
	/*IObjectBase::SetPropertyValue("bitmapsize", m_bitmapsize);
	IObjectBase::SetPropertyValue("margins", m_margins);
	IObjectBase::SetPropertyValue("packing", m_packing);
	IObjectBase::SetPropertyValue("separation", m_separation);*/

	IObjectBase::SetPropertyValue("action_source", m_actionSource);
}

void CValueToolbar::SaveProperty()
{
	IValueWindow::SaveProperty();

	IObjectBase::GetPropertyValue("name", m_controlName);
	/*IObjectBase::GetPropertyValue("bitmapsize", m_bitmapsize);
	IObjectBase::GetPropertyValue("margins", m_margins);
	IObjectBase::GetPropertyValue("packing", m_packing);
	IObjectBase::GetPropertyValue("separation", m_separation);*/

	IObjectBase::GetPropertyValue("action_source", m_actionSource);
}