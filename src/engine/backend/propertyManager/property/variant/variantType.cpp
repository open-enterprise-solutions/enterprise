#include "variantType.h"
#include "backend/metaData.h"

wxString ibVariantDataAttribute::MakeString() const
{
	wxString strDescr;
	if (m_ownerProperty != nullptr) {
		// The same owner as DoRefreshTypeDesc, and the same answer when it has no configuration: the value
		// registry names what it holds itself.
		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		for (const auto clsid : m_typeDesc.GetClsidList()) {
			const bool known = metaData != nullptr ? metaData->IsRegisterCtor(clsid) : ibValue::IsRegisterCtor(clsid);
			if (!known)
				continue;
			const wxString name = metaData != nullptr ? metaData->GetNameObjectFromID(clsid) : ibValue::GetNameObjectFromID(clsid);
			strDescr = strDescr.IsEmpty() ? name : strDescr + wxT(", ") + name;
		}
	}
	return strDescr;
}

void ibVariantDataAttribute::DoSetDefaultMetaType() {
	if (m_ownerProperty != nullptr)
		m_typeDesc.SetDefaultMetaType(ibBackendTypeConfigFactory::GetDefaultTypeByFilter(m_ownerProperty->GetFilterDataType()));
}

void ibVariantDataAttribute::DoSetFromMetaId(const ibMetaID& id)
{
	if (m_ownerProperty != nullptr && id != wxNOT_FOUND) {

		// An attribute is found in a configuration; an owner that has none (see DoRefreshTypeDesc) finds none.
		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		if (metaData == nullptr) {
			SetDefaultMetaType();
			return;
		}

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

		// 🛑 THE OWNER NEED NOT BELONG TO A CONFIGURATION. A value table's column answers the ACTIVE one, and a
		// process may have none — a test, a headless tool before it opens a base. The check below then read the
		// configuration through nothing, and giving a table's column a type there was an access violation
		// (ValueTableVerbs.AColumnAddedWithoutAType_KeepsTextOfAnyLength, on every platform of CI, 2026-09-22):
		// the untyped columns every other test used never came here. Without a configuration there is nothing a
		// type can have been removed from — the value registry's own types stand as given, the same rule
		// ibValueTypeDescription::AdjustValue keeps for the same process.
		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		if (metaData == nullptr) {
			if (!m_typeDesc.IsOk()) SetDefaultMetaType();
			return;
		}

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