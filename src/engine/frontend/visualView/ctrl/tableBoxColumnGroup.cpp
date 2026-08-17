////////////////////////////////////////////////////////////////////////////
// Name:        tableBoxColumnGroup.cpp
// Purpose:     a group of table columns — a header with an orientation
// Author:      Maxim Kornienko
////////////////////////////////////////////////////////////////////////////

#include "tableBox.h"
#include "form.h"

#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/appData.h"

#ifndef OES_USE_WEB
// The runtime participants (column object + group object) live here.
#include "tableBoxColumnRenderer.h"
#else
#include "frontend/web/webWindow.h"
#endif

//***********************************************************************************
//*                         ibValueModelTableBoxColumnGroup                          *
//***********************************************************************************

ibValueModelTableBoxColumnGroup::ibValueModelTableBoxColumnGroup()
	: ibValueControl()
{
}

void ibValueModelTableBoxColumnGroup::AddColumn()
{
#ifndef OES_USE_WEB
	wxASSERT(m_formOwner);

	ibValueFrame* newColumn = m_formOwner->NewObject(g_controlTableBoxColumnCLSID, this);
	g_visualHostContext->InsertControl(newColumn, this);
	g_visualHostContext->RefreshEditor();
#endif
}

void ibValueModelTableBoxColumnGroup::AddColumnGroup()
{
#ifndef OES_USE_WEB
	wxASSERT(m_formOwner);

	ibValueFrame* newColumnGroup = m_formOwner->NewObject(g_controlTableBoxColumnGroupCLSID, this);
	g_visualHostContext->InsertControl(newColumnGroup, this);
	g_visualHostContext->RefreshEditor();
#endif
}

// THE TABLE THIS GROUP SERVES — the same one-step-at-a-time walk a column does (groups nest,
// so the answer is always "my parent's answer" until a table says itself).
ibValueModelTableBox* ibValueModelTableBoxColumnGroup::GetOwner() const
{
	if (ibValueModelTableBox* table = dynamic_cast<ibValueModelTableBox*>(m_parent))
		return table;
	if (ibValueModelTableBoxColumnGroup* group = dynamic_cast<ibValueModelTableBoxColumnGroup*>(m_parent))
		return group->GetOwner();
	return nullptr;
}

#ifndef OES_USE_WEB
// THE COLUMN STORE THIS GROUP IS — what its own members hang on.
ibDataViewColumnGroup* ibValueModelTableBoxColumnGroup::GetColumnGroup() const
{
	return dynamic_cast<ibDataViewColumnGroup*>(GetWxObject());
}
#endif // !OES_USE_WEB

const ibMetaData* ibValueModelTableBoxColumnGroup::GetMetaData() const
{
	return m_formOwner != nullptr ? m_formOwner->GetMetaData() : nullptr;
}

wxString ibValueModelTableBoxColumnGroup::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty())
		return m_propertyTitle->GetValueAsTranslateString();

	return m_propertyName->GetValueAsString();
}

//***********************************************************************************
//*                                  Control                                         *
//***********************************************************************************

wxObject* ibValueModelTableBoxColumnGroup::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent; (void)visualHost;
	return new ibWebStubControl(wxT("tableboxcolumngroup"));
#else
	return new ibDataViewColumnGroupObject(this, GetControlTitle(), GetGrouping(),
		(wxAlignment)m_propertyHeaderAlign->GetValueAsEnum());
#endif
}

void ibValueModelTableBoxColumnGroup::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
#ifndef OES_USE_WEB
	ibDataViewColumnGroup* group = dynamic_cast<ibDataViewColumnGroup*>(wxobject);
	if (group == nullptr)
		return;

	// Hung on its holder exactly as a column is: a group inside a group, otherwise the
	// table's root group.
	if (ibDataViewColumnGroup* holder = ibFindColumnHolder(m_parent))
		holder->AppendGroup(group);
#endif
}

void ibValueModelTableBoxColumnGroup::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibDataViewColumnGroup* group = dynamic_cast<ibDataViewColumnGroup*>(wxobject);
	if (group == nullptr)
		return;

	group->SetTitle(GetControlTitle());
	group->SetKind(GetGrouping());
	group->SetTitleShown(m_propertyShowTitle->GetValueAsBoolean());
	group->SetAlignment((wxAlignment)m_propertyHeaderAlign->GetValueAsEnum());
	group->SetHidden(!m_propertyVisible->GetValueAsBoolean());

	// The geometry the group decides (row bands, header depth) is the CONTROL's to
	// re-derive — the group is only data. The control is asked OF THE GROUP.
	if (ibDataViewCtrl* dataViewCtrl = group->GetOwner())
		dataViewCtrl->InvalidateColumnLayout();
#endif
}

void ibValueModelTableBoxColumnGroup::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
#ifndef OES_USE_WEB
	ibDataViewColumnGroup* group = dynamic_cast<ibDataViewColumnGroup*>(obj);
	if (group == nullptr)
		return;

	// The control is taken straight OFF THE GROUP (set at AddColumnGroup) — reliable
	// during teardown, unlike GetOwner()->GetInnerWx(), which resolves through the
	// dying host and returns null. That null is what made the detach below silently
	// not happen: the group stayed in the control's list and was freed a second time
	// in ~ibDataViewCtrl, after the host had already deleted the object. Same reason
	// the column's Cleanup reads its control off the column.
	ibDataViewCtrl* dataViewCtrl = group->GetOwner();
	if (dataViewCtrl == nullptr)
		return;

	// DETACH — re-points whatever was under it at this group's own parent, so nothing
	// is left pointing at freed memory. The object itself is freed by the host.
	dataViewCtrl->RemoveColumnGroup(group);
#endif
}

//***********************************************************************************
//*                                    Menu                                          *
//***********************************************************************************

enum
{
	MENU_GROUP_ADD_COLUMN = 1000,
	MENU_GROUP_ADD_GROUP
};

void ibValueModelTableBoxColumnGroup::PrepareDefaultMenu(wxMenu* menu)
{
	menu->Append(MENU_GROUP_ADD_COLUMN, _("Add column\tInsert"))->SetBitmap(ibValueModelTableBoxColumn::GetIconGroup());
	menu->Append(MENU_GROUP_ADD_GROUP, _("Add column group"))->SetBitmap(ibValueModelTableBoxColumnGroup::GetIconGroup());
	menu->AppendSeparator();
}

void ibValueModelTableBoxColumnGroup::ExecuteMenu(ibVisualHost* visualHost, int id)
{
	switch (id)
	{
	case MENU_GROUP_ADD_COLUMN:
		AddColumn();
		break;
	case MENU_GROUP_ADD_GROUP:
		AddColumnGroup();
		break;
	}
}

//***********************************************************************************
//*                                    Data                                          *
//***********************************************************************************

bool ibValueModelTableBoxColumnGroup::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyGrouping->SetNodeValue(node.GetProperty(m_propertyGrouping->GetName()));
	m_propertyShowTitle->SetNodeValue(node.GetProperty(m_propertyShowTitle->GetName()));
	m_propertyHeaderAlign->SetNodeValue(node.GetProperty(m_propertyHeaderAlign->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueModelTableBoxColumnGroup::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyGrouping->GetName(), m_propertyGrouping->GetNodeValue());
	node.SetProperty(m_propertyShowTitle->GetName(), m_propertyShowTitle->GetNodeValue());
	node.SetProperty(m_propertyHeaderAlign->GetName(), m_propertyHeaderAlign->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueControl::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

ENUM_TYPE_REGISTER(ibValueEnumTableBoxColumnGrouping, "TableboxColumnGrouping", enum_to_clsid("EN_TBXCG"));
S_CONTROL_TYPE_REGISTER(ibValueModelTableBoxColumnGroup, "TableboxColumnGroup", "TableboxColumn", g_controlTableBoxColumnGroupCLSID);
