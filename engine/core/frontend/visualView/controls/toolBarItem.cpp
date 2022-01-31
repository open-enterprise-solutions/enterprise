#include "toolbar.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueToolBarItem, IValueControl);
wxIMPLEMENT_DYNAMIC_CLASS(CValueToolBarSeparator, IValueControl);

//***********************************************************************************
//*                           CValueToolBarItem                               *
//***********************************************************************************

CValueToolBarItem::CValueToolBarItem() : IValueControl(),
m_caption("newTool"),
m_action(wxEmptyString),
m_enabled(true),
m_context_menu(false)
{
	PropertyContainer *categoryToolbar = IObjectBase::CreatePropertyContainer("ToolBarItem");
	categoryToolbar->AddProperty("name", PropertyType::PT_WXNAME);
	categoryToolbar->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryToolbar->AddProperty("bitmap", PropertyType::PT_BITMAP);
	categoryToolbar->AddProperty("context_menu", PropertyType::PT_BOOL);
	categoryToolbar->AddProperty("tooltip", PropertyType::PT_WXSTRING);
	categoryToolbar->AddProperty("enabled", PropertyType::PT_BOOL);
	categoryToolbar->AddProperty("action", PropertyType::PT_TOOL_ACTION, &CValueToolBarItem::GetActions);
	m_category->AddCategory(categoryToolbar);
}

void CValueToolBarItem::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	CAuiToolBar *m_auiToolWnd = dynamic_cast<CAuiToolBar *>(visualHost->GetWxObject(GetParent()));
	wxASSERT(m_auiToolWnd);

	wxAuiToolBarItem* m_toolItem = m_auiToolWnd->AddTool(GetControlID(),
		m_caption,
		m_bitmap,
		wxNullBitmap,
		wxItemKind::wxITEM_NORMAL,
		m_tooltip,
		wxEmptyString,
		wxobject
	);

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	m_toolItem->SetUserData((long long)wxobject);
#else 
	m_toolItem->SetUserData((long)wxobject);
#endif 

	m_auiToolWnd->EnableTool(m_toolItem->GetId(), m_enabled);

	if (m_context_menu == 1 && !m_toolItem->HasDropDown())
		m_auiToolWnd->SetToolDropDown(m_toolItem->GetId(), true);
	else if (m_context_menu == 0 && m_toolItem->HasDropDown())
		m_auiToolWnd->SetToolDropDown(m_toolItem->GetId(), false);

	m_auiToolWnd->Realize();
	m_auiToolWnd->Refresh();
	m_auiToolWnd->Update();
}

void CValueToolBarItem::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	CAuiToolBar *m_auiToolWnd = dynamic_cast<CAuiToolBar *>(visualHost->GetWxObject(GetParent()));
	wxASSERT(m_auiToolWnd);

	wxAuiToolBarItem *m_toolItem = m_auiToolWnd->FindTool(GetControlID());
	IValueFrame *m_parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < m_parentControl->GetChildCount(); i++)
	{
		IValueFrame *child = m_parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) { idx = i; break; }
	}

	if (m_toolItem) { m_auiToolWnd->DeleteTool(GetControlID()); }

	m_toolItem = m_auiToolWnd->InsertTool(idx, GetControlID(),
		m_caption,
		m_bitmap,
		wxNullBitmap,
		wxItemKind::wxITEM_NORMAL,
		m_tooltip,
		wxEmptyString,
		wxobject
	);

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	m_toolItem->SetUserData((long long)wxobject);
#else 
	m_toolItem->SetUserData((long)wxobject);
#endif 

	m_auiToolWnd->EnableTool(m_toolItem->GetId(), m_enabled);

	if (m_context_menu == true && !m_toolItem->HasDropDown())
		m_auiToolWnd->SetToolDropDown(m_toolItem->GetId(), true);
	else if (m_context_menu == false && m_toolItem->HasDropDown())
		m_auiToolWnd->SetToolDropDown(m_toolItem->GetId(), false);

	m_auiToolWnd->Realize();
	m_auiToolWnd->Refresh();
	m_auiToolWnd->Update();
}

void CValueToolBarItem::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
	CAuiToolBar *m_auiToolWnd = dynamic_cast<CAuiToolBar *>(visualHost->GetWxObject(GetParent()));

	m_auiToolWnd->DeleteTool(GetControlID());

	m_auiToolWnd->Realize();
	m_auiToolWnd->Refresh();
	m_auiToolWnd->Update();
}

bool CValueToolBarItem::CanDeleteControl() const
{
	unsigned int toolCount = 0;
	for (unsigned int idx = 0; idx < m_parent->GetChildCount(); idx++) {
		auto child = m_parent->GetChild(idx);
		if (wxT("tool") == child->GetClassName()) {
			toolCount++;
		}
	}

	return toolCount > 1;
}

//***********************************************************************************
//*                           CValueToolBarSeparator                                *
//***********************************************************************************

CValueToolBarSeparator::CValueToolBarSeparator() : IValueControl()
{
	PropertyContainer *categoryToolbar = IObjectBase::CreatePropertyContainer("ToolBarItem");
	categoryToolbar->AddProperty("name", PropertyType::PT_WXNAME);
	m_category->AddCategory(categoryToolbar);
}

void CValueToolBarSeparator::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	CAuiToolBar *m_auiToolWnd = dynamic_cast<CAuiToolBar *>(visualHost->GetWxObject(GetParent()));
	wxASSERT(m_auiToolWnd);
	wxAuiToolBarItem *m_toolItem = m_auiToolWnd->AddSeparator();
	m_toolItem->SetId(GetControlID());

	m_auiToolWnd->Realize();
	m_auiToolWnd->Refresh();
	m_auiToolWnd->Update();
}

void CValueToolBarSeparator::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	CAuiToolBar *m_auiToolWnd = dynamic_cast<CAuiToolBar *>(visualHost->GetWxObject(GetParent()));
	wxASSERT(m_auiToolWnd);

	wxAuiToolBarItem *m_toolItem = m_auiToolWnd->FindTool(GetControlID());
	IValueFrame *m_parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < m_parentControl->GetChildCount(); i++)
	{
		IValueFrame *child = m_parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) { idx = i; break; }
	}

	if (m_toolItem) { m_auiToolWnd->DeleteTool(GetControlID()); }
	m_auiToolWnd->InsertSeparator(idx, GetControlID());

	m_auiToolWnd->Realize();
	m_auiToolWnd->Refresh();
	m_auiToolWnd->Update();
}

void CValueToolBarSeparator::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
	CAuiToolBar *m_auiToolWnd = dynamic_cast<CAuiToolBar *>(visualHost->GetWxObject(GetParent()));
	if (m_auiToolWnd) m_auiToolWnd->DeleteTool(GetControlID());
}

bool CValueToolBarSeparator::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

//**********************************************************************************
//*                                  Data	                                       *
//**********************************************************************************

bool CValueToolBarItem::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_caption);
	m_context_menu = reader.r_u8();
	reader.r_stringZ(m_tooltip);
	m_enabled = reader.r_u8();
	reader.r_stringZ(m_action);

	return IValueControl::LoadData(reader);
}

bool CValueToolBarItem::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_caption);
	writer.w_u8(m_context_menu);
	writer.w_stringZ(m_tooltip);
	writer.w_u8(m_enabled);
	writer.w_stringZ(m_action);

	return IValueControl::SaveData(writer);
}

bool CValueToolBarSeparator::LoadData(CMemoryReader &reader)
{
	return IValueControl::LoadData(reader);
}

bool CValueToolBarSeparator::SaveData(CMemoryWriter &writer)
{
	return IValueControl::SaveData(writer);
}

//**********************************************************************************
//*                                  Property                                      *
//**********************************************************************************

void CValueToolBarItem::ReadProperty()
{
	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("caption", m_caption);
	IObjectBase::SetPropertyValue("bitmap", m_bitmap);
	IObjectBase::SetPropertyValue("context_menu", m_context_menu);
	IObjectBase::SetPropertyValue("tooltip", m_tooltip);
	IObjectBase::SetPropertyValue("enabled", m_enabled);

	IObjectBase::SetPropertyValue("action", m_action);
}

void CValueToolBarItem::SaveProperty()
{
	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);
	IObjectBase::GetPropertyValue("bitmap", m_bitmap);
	IObjectBase::GetPropertyValue("context_menu", m_context_menu);
	IObjectBase::GetPropertyValue("tooltip", m_tooltip);
	IObjectBase::GetPropertyValue("enabled", m_enabled);

	IObjectBase::GetPropertyValue("action", m_action);
}

void CValueToolBarSeparator::ReadProperty()
{
	IObjectBase::SetPropertyValue("name", m_controlName);
}

void CValueToolBarSeparator::SaveProperty()
{
	IObjectBase::GetPropertyValue("name", m_controlName);
}
