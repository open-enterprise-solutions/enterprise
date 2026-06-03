#include "variantType.h"
#include "backend/metaData.h"

wxString ibVariantDataAttribute::MakeString() const
{
	wxString strDescr;
	if (m_ownerProperty != nullptr) {
		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		wxASSERT(metaData);
		for (const auto clsid : m_typeDesc.GetClsidList()) {
			if (metaData->IsRegisterCtor(clsid) && strDescr.IsEmpty()) {
				strDescr = metaData->GetNameObjectFromID(clsid);
			}
			else if (metaData->IsRegisterCtor(clsid)) {
				strDescr = strDescr +
					wxT(", ") + metaData->GetNameObjectFromID(clsid);
			}
		}
	}
	return strDescr;
}

#include "backend/system/value/valueTable.h"

void ibVariantDataAttribute::DoSetDefaultMetaType() {
	if (m_ownerProperty != nullptr) {
		if (ibSelectorDataType::ibSelectorDataType_boolean == m_ownerProperty->GetFilterDataType()) {
			m_typeDesc.SetDefaultMetaType(g_valueBooleanCLSID);
		}
		else if (ibSelectorDataType::ibSelectorDataType_reference == m_ownerProperty->GetFilterDataType()) {
			m_typeDesc.SetDefaultMetaType(g_valueStringCLSID);
		}
		else if (ibSelectorDataType::ibSelectorDataType_resource == m_ownerProperty->GetFilterDataType()) {
			m_typeDesc.SetDefaultMetaType(g_valueNumberCLSID);
		}
		else if (ibSelectorDataType::ibSelectorDataType_table == m_ownerProperty->GetFilterDataType()) {
			m_typeDesc.SetDefaultMetaType(g_valueTableCLSID);
		}
		else {
			m_typeDesc.SetDefaultMetaType(g_valueStringCLSID);
		}
	}
}

void ibVariantDataAttribute::DoSetFromMetaId(const ibMetaID& id)
{
	if (m_ownerProperty != nullptr && id != wxNOT_FOUND) {

		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		wxASSERT(metaData);

		const ibValueMetaObjectAttributeBase* attribute = metaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(id, true);
		if (attribute != nullptr && attribute->IsAllowed()) {
			m_typeDesc.SetDefaultMetaType(attribute->GetTypeDesc());
			return;
		}

		const ibValueMetaObjectTableData* metaTable = metaData->FindAnyObjectByFilter<ibValueMetaObjectTableData>(id, true);
		if (metaTable != nullptr && metaTable->IsAllowed()) {
			m_typeDesc.SetDefaultMetaType(metaTable->GetTypeDesc());
			return;
		}

		SetDefaultMetaType();
	}
}

void ibVariantDataAttribute::DoSetFromTypeId(const ibTypeDescription& td)
{
	m_typeDesc = td;
	RefreshTypeDesc();
}

void ibVariantDataAttribute::DoRefreshTypeDesc()
{
	if (m_ownerProperty != nullptr) {

		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		wxASSERT(metaData);

		const unsigned int object_version = metaData->GetFactoryCountChanges();
		if (object_version != m_object_version) {

			for (const auto clsid : m_typeDesc.GetClsidList()) {

				if (!metaData->IsRegisterCtor(clsid))
					m_typeDesc.ClearMetaType(clsid);

				if (m_typeDesc.GetClsidCount() == 0)
					break;
			}

			m_object_version = object_version;
		}

		if (!m_typeDesc.IsOk()) SetDefaultMetaType();
	}
}