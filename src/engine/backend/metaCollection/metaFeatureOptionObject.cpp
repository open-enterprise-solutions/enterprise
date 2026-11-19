////////////////////////////////////////////////////////////////////////////
//	Description : feature option - a stored value that decides what is available
////////////////////////////////////////////////////////////////////////////

#include "metaFeatureOptionObject.h"
#include "backend/featureOption/featureOptionGate.h"

#include "backend/metaData.h"

//***********************************************************************
//*                         metaData                                    *
//***********************************************************************

ibValueMetaObjectFeatureOption::ibValueMetaObjectFeatureOption() : ibValueMetaObjectStoredValue()
{
}

bool ibValueMetaObjectFeatureOption::IsOn() const
{
	const ibValue stored = ReadStoredValue();
	return stored.GetType() != ibValueTypes::TYPE_BOOLEAN || stored.GetBoolean();
}

ibValue ibValueMetaObjectFeatureOption::CreateValue() const
{
	return ibValue(m_propertyInitialValue->GetValueAsBoolean());
}

ibMetaID ibValueMetaObjectFeatureOption::GetRequired() const
{
	const ibMetaDescription& required = m_propertyRequires->GetValueAsMetaDesc();
	return required.GetTypeCount() > 0 ? required.GetByIdx(0) : ibNOT_FOUND;
}

bool ibValueMetaObjectFeatureOption::ReadData(const ibDataNode& node)
{
	m_propertyInitialValue->SetNodeValue(node.GetProperty(m_propertyInitialValue->GetName()));
	m_propertyRequires->SetNodeValue(node.GetProperty(m_propertyRequires->GetName()));
	return ibValueMetaObjectStoredValue::ReadData(node);
}

bool ibValueMetaObjectFeatureOption::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyInitialValue->GetName(), m_propertyInitialValue->GetNodeValue());
	node.SetProperty(m_propertyRequires->GetName(), m_propertyRequires->GetNodeValue());
	return ibValueMetaObjectStoredValue::WriteData(node);
}

void ibValueMetaObjectFeatureOption::OnAfterValueWrite() const
{
	ibFeatureOptionGate::Forget(m_metaData);
}

// A deleted option leaves no one belonging to it: the members let its id go with it.
bool ibValueMetaObjectFeatureOption::OnDeleteMetaObject()
{
	if (m_metaData != nullptr) {
		for (ibValueMetaObject* object : m_metaData->GetAnyArrayObject(/*use_child_filter*/ true)) {
			if (object->IsInFeatureOption(m_metaId))
				object->SetFeatureOption(m_metaId, false);
		}
	}
	return ibValueMetaObjectStoredValue::OnDeleteMetaObject();
}

// An application opened again may be a different one at the same address — nothing answered about the
// previous one may be read for it.
bool ibValueMetaObjectFeatureOption::OnBeforeRunMetaObject(int flags)
{
	ibFeatureOptionGate::Forget(m_metaData);
	return ibValueMetaObjectStoredValue::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectFeatureOption::OnAfterCloseMetaObject()
{
	ibFeatureOptionGate::Forget(m_metaData);
	return ibValueMetaObjectStoredValue::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectFeatureOption, "FeatureOption", g_metaFeatureOptionCLSID);
