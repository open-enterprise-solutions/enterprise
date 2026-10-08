////////////////////////////////////////////////////////////////////////////
// Name:        tableBoxColumnGroup.cpp
// Purpose:     a group of table columns — a header with an orientation
// Author:      Maxim Kornienko
////////////////////////////////////////////////////////////////////////////

#include "tableBox.h"
#include "form.h"

#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                         ibValueModelTableBoxColumnGroup                          *
//***********************************************************************************

ibValueModelTableBoxColumnGroup::ibValueModelTableBoxColumnGroup()
	: ibValueControl()
{
}

void ibValueModelTableBoxColumnGroup::AddColumn()
{
	wxASSERT(m_formOwner);

	m_formOwner->NewObject(g_controlTableBoxColumnCLSID, this);
}

void ibValueModelTableBoxColumnGroup::AddColumnGroup()
{
	wxASSERT(m_formOwner);

	m_formOwner->NewObject(g_controlTableBoxColumnGroupCLSID, this);
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

bool ibValueModelTableBoxColumnGroup::IsGroupShown() const
{
	return m_propertyVisible->GetValueAsBoolean() && IsAvailable();
}

//***********************************************************************************
//*                                  Update                                          *
//***********************************************************************************

void ibValueModelTableBoxColumnGroup::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	// The title in the header (its own, else its name — shown only when ShowTitle is on, or always for an
	// in-cell group), and whether the group and everything it holds is shown. The geometry it decides — row
	// bands, header depth — is the client's to derive from the kinds as saved.
	state.SetValue(wxT("Caption"), GetControlTitle());
	state.SetValue(wxT("Visible"), IsGroupShown());
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
