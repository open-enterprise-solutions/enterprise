#include "toolbar.h"
#include "backend/appData.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueToolBarItem, ibValueControl);
wxIMPLEMENT_DYNAMIC_CLASS(ibValueToolBarSeparator, ibValueControl);

//***********************************************************************************
//*                           ibValueToolBarItem                               *
//***********************************************************************************

ibValueToolBarItem::ibValueToolBarItem() : ibValueControl()
{
}

void ibValueToolBarItem::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(wxparent);
	wxASSERT(toolbar);
	wxAuiToolBarItem* toolItem = toolbar->AddTool(GetControlID(),
		m_propertyTitle->GetValueAsTranslateString(),
		m_propertyPicture->GetValueAsBitmap(),
		wxNullBitmap,
		wxItemKind::wxITEM_NORMAL,
		m_properyTooltip->GetValueAsTranslateString(),
		wxEmptyString,
		wxobject
	);

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	toolItem->SetUserData((long long)wxobject);
#else 
	toolItem->SetUserData((long)wxobject);
#endif 

	toolbar->EnableTool(toolItem->GetId(), m_propertyEnabled->GetValueAsBoolean());

	if (m_propertyContextMenu->GetValueAsBoolean() == true && !toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), true);
	else if (m_propertyContextMenu->GetValueAsBoolean() == false && toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), false);

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
}

void ibValueToolBarItem::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(wxparent);
	wxASSERT(toolbar);

	wxAuiToolBarItem* toolItem = toolbar->FindTool(GetControlID());
	ibValueFrame* parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		ibValueFrame* child = parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) {
			idx = i;
			break;
		}
	}

	if (toolItem != nullptr)
		toolbar->DestroyTool(GetControlID());

	if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Auto) {
		const ibActionCollection& collection = GetOwner()->GetActionArray();
		if (GetItemRepresentation(collection) == ibRepresentation::ibRepresentation_PictureAndText) {
			toolItem = toolbar->InsertTool(idx, GetControlID(),
				GetItemCaption(collection),
				GetItemPicture(collection),
				wxNullBitmap,
				wxItemKind::wxITEM_NORMAL,
				GetItemToolTip(collection),
				wxEmptyString,
				wxobject
			);
		}
		else if (GetItemRepresentation(collection) == ibRepresentation::ibRepresentation_Picture) {
			toolItem = toolbar->InsertTool(idx, GetControlID(),
				wxEmptyString,
				GetItemPicture(collection),
				wxNullBitmap,
				wxItemKind::wxITEM_NORMAL,
				GetItemToolTip(collection),
				wxEmptyString,
				wxobject
			);
		}
		else if (GetItemRepresentation(collection) == ibRepresentation::ibRepresentation_Text) {
			toolItem = toolbar->InsertTool(idx, GetControlID(),
				GetItemCaption(collection),
				wxNullBitmap,
				wxNullBitmap,
				wxItemKind::wxITEM_NORMAL,
				GetItemToolTip(collection),
				wxEmptyString,
				wxobject
			);
		}
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_PictureAndText) {
		const ibActionCollection& collection = GetOwner()->GetActionArray();
		toolItem = toolbar->InsertTool(idx, GetControlID(),
			GetItemCaption(collection),
			GetItemPicture(collection),
			wxNullBitmap,
			wxItemKind::wxITEM_NORMAL,
			GetItemToolTip(collection),
			wxEmptyString,
			wxobject
		);
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Picture) {
		const ibActionCollection& collection = GetOwner()->GetActionArray();
		toolItem = toolbar->InsertTool(idx, GetControlID(),
			wxEmptyString,
			GetItemPicture(collection),
			wxNullBitmap,
			wxItemKind::wxITEM_NORMAL,
			GetItemToolTip(collection),
			wxEmptyString,
			wxobject
		);
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Text) {
		const ibActionCollection& collection = GetOwner()->GetActionArray();
		toolItem = toolbar->InsertTool(idx, GetControlID(),
			GetItemCaption(collection),
			wxNullBitmap,
			wxNullBitmap,
			wxItemKind::wxITEM_NORMAL,
			GetItemToolTip(collection),
			wxEmptyString,
			wxobject
		);
	}

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	toolItem->SetUserData((long long)wxobject);
#else 
	toolItem->SetUserData((long)wxobject);
#endif 

	toolbar->EnableTool(toolItem->GetId(), m_propertyEnabled->GetValueAsBoolean());

	if (m_propertyContextMenu->GetValueAsBoolean() == true && !toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), true);
	else if (m_propertyContextMenu->GetValueAsBoolean() == false && toolItem->HasDropDown())
		toolbar->SetToolDropDown(toolItem->GetId(), false);

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();

	toolbar->InvalidateBestSize();
}

void ibValueToolBarItem::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));

	toolbar->DestroyTool(GetControlID());

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
}

bool ibValueToolBarItem::CanDeleteControl() const
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
//*                           ibValueToolBarSeparator                                *
//***********************************************************************************

ibValueToolBarSeparator::ibValueToolBarSeparator() : ibValueControl()
{
}

void ibValueToolBarSeparator::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));
	wxASSERT(toolbar);
	wxAuiToolBarItem* toolItem = toolbar->AddSeparator();
	toolItem->SetId(GetControlID());

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
}

void ibValueToolBarSeparator::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));
	wxASSERT(toolbar);

	wxAuiToolBarItem* toolItem = toolbar->FindTool(GetControlID());
	ibValueFrame* m_parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < m_parentControl->GetChildCount(); i++)
	{
		ibValueFrame* child = m_parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) { idx = i; break; }
	}

	if (toolItem) { toolbar->DestroyTool(GetControlID()); }
	toolbar->InsertSeparator(idx, GetControlID());

	toolbar->Realize();
	if (!appData->DesignerMode() || !visualHost->IsDesignerHost())
		toolbar->SetMinSize(wxSize(-1, toolbar->GetMinHeight()));
	toolbar->Refresh();
	toolbar->Update();
}

void ibValueToolBarSeparator::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
	ibAuiToolBar* toolbar = dynamic_cast<ibAuiToolBar*>(visualHost->GetWxObject(GetParent()));
	if (toolbar) toolbar->DestroyTool(GetControlID());
}

bool ibValueToolBarSeparator::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

//**********************************************************************************
//*                                  Data	                                       *
//**********************************************************************************

bool ibValueToolBarItem::LoadData(ibReaderMemory& reader)
{
	m_propertyTitle->LoadData(reader);
	m_propertyPicture->LoadData(reader);
	m_propertyRepresentation->LoadData(reader);
	m_propertyContextMenu->LoadData(reader);
	m_properyTooltip->LoadData(reader);
	m_propertyEnabled->LoadData(reader);
	m_eventAction->LoadData(reader);

	//events 
	m_eventAction->LoadData(reader);
	return ibValueControl::LoadData(reader);
}

bool ibValueToolBarItem::SaveData(ibWriterMemory writer)
{
	m_propertyTitle->SaveData(writer);
	m_propertyPicture->SaveData(writer);
	m_propertyRepresentation->SaveData(writer);
	m_propertyContextMenu->SaveData(writer);
	m_properyTooltip->SaveData(writer);
	m_propertyEnabled->SaveData(writer);
	m_eventAction->SaveData(writer);

	//events 
	m_eventAction->SaveData(writer);
	return ibValueControl::SaveData(writer);
}

bool ibValueToolBarSeparator::LoadData(ibReaderMemory& reader)
{
	return ibValueControl::LoadData(reader);
}

bool ibValueToolBarSeparator::SaveData(ibWriterMemory writer)
{
	return ibValueControl::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueToolBarItem, "Tool", "Tool", g_controlToolBarItemCLSID);
S_CONTROL_TYPE_REGISTER(ibValueToolBarSeparator, "ToolSeparator", "Tool", g_controlToolBarSeparatorCLSID);