////////////////////////////////////////////////////////////////////////////
//	Description : functional option - a stored value that decides what is available
////////////////////////////////////////////////////////////////////////////

#include "metaFunctionalOptionObject.h"
#include "backend/functionalOption/functionalOptionGate.h"

#include "backend/metaData.h"

//***********************************************************************
//*                         metaData                                    *
//***********************************************************************

ibValueMetaObjectFunctionalOption::ibValueMetaObjectFunctionalOption() : ibValueMetaObjectStoredValue()
{
}

bool ibValueMetaObjectFunctionalOption::IsOn() const
{
	const ibValue stored = ReadStoredValue();
	return stored.GetType() != ibValueTypes::TYPE_BOOLEAN || stored.GetBoolean();
}

ibValue ibValueMetaObjectFunctionalOption::CreateValue() const
{
	return ibValue(m_propertyInitialValue->GetValueAsBoolean());
}

ibMetaID ibValueMetaObjectFunctionalOption::GetRequired() const
{
	const ibMetaDescription& required = m_propertyRequires->GetValueAsMetaDesc();
	return required.GetTypeCount() > 0 ? required.GetByIdx(0) : wxNOT_FOUND;
}

bool ibValueMetaObjectFunctionalOption::ReadData(const ibDataNode& node)
{
	m_propertyInitialValue->SetNodeValue(node.GetProperty(m_propertyInitialValue->GetName()));
	m_propertyRequires->SetNodeValue(node.GetProperty(m_propertyRequires->GetName()));
	return ibValueMetaObjectStoredValue::ReadData(node);
}

bool ibValueMetaObjectFunctionalOption::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyInitialValue->GetName(), m_propertyInitialValue->GetNodeValue());
	node.SetProperty(m_propertyRequires->GetName(), m_propertyRequires->GetNodeValue());
	return ibValueMetaObjectStoredValue::WriteData(node);
}

void ibValueMetaObjectFunctionalOption::OnAfterValueWrite() const
{
	ibFunctionalOptionGate::Forget(m_metaData);
}

// A deleted option leaves no one belonging to it: the members let its id go with it.
bool ibValueMetaObjectFunctionalOption::OnDeleteMetaObject()
{
	if (m_metaData != nullptr) {
		for (ibValueMetaObject* object : m_metaData->GetAnyArrayObject(/*use_child_filter*/ true)) {
			if (object->IsInFunctionalOption(m_metaId))
				object->SetFunctionalOption(m_metaId, false);
		}
	}
	return ibValueMetaObjectStoredValue::OnDeleteMetaObject();
}

// A configuration opened again may be a different one at the same address — nothing answered about the
// previous one may be read for it.
bool ibValueMetaObjectFunctionalOption::OnBeforeRunMetaObject(int flags)
{
	ibFunctionalOptionGate::Forget(m_metaData);
	return ibValueMetaObjectStoredValue::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectFunctionalOption::OnAfterCloseMetaObject()
{
	ibFunctionalOptionGate::Forget(m_metaData);
	return ibValueMetaObjectStoredValue::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectFunctionalOption, "FunctionalOption", g_metaFunctionalOptionCLSID);
