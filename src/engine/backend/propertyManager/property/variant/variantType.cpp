#include "variantType.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"   // ibCtorMetaValueType — the configuration's references a barrier stands for

wxString ibVariantDataAttribute::MakeString() const
{
	wxString strDescr;
	if (m_ownerProperty != nullptr) {
		// The same owner as DoRefreshTypeDesc, and the same answer when it has no configuration: the value
		// registry names what it holds itself.
		const ibMetaData* metaData = m_ownerProperty->GetMetaData();
		for (const auto clsid : m_typeDesc.GetClsidList()) {
			// One look-up answers both: whether the type is known, and its name.
			const ibCtorAbstractType* typeCtor = metaData != nullptr ? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
			if (typeCtor == nullptr)
				continue;
			strDescr = strDescr.IsEmpty() ? typeCtor->GetClassName() : strDescr + wxT(", ") + typeCtor->GetClassName();
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
	m_object_version = 0;   // a new declaration is refreshed as a new value is — its barriers derived again
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

			// ONE WALK, over a copy — the declaration loses a type no longer registered as it goes. What a value may
			// be is built beside it: a barrier stands for its members — `AnyRef` for every reference of the
			// configuration, `DocumentRef` for every document's, the ones its bits admit (clsid_admits) — and one
			// with none to stand for (no document yet, a configuration only loaded) stands for itself. Left empty
			// when no barrier was replaced: the declaration answers then.
			const std::vector<ibClassID> declared = m_typeDesc.GetClsidList();
			m_typeValueDesc = ibTypeDescription();
			bool replaced = false;
			for (const auto clsid : declared) {

				if (!metaData->IsRegisterCtor(clsid)) {
					m_typeDesc.ClearMetaType(clsid);
					continue;
				}

				std::vector<ibClassID> members;
				if (IsReference(clsid) && clsid_is_any(clsid))
					for (const ibCtorMetaValueType* reference : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
						if (clsid_admits(clsid, reference->GetClassType()))
							members.push_back(reference->GetClassType());

				if (members.empty())
					m_typeValueDesc.AppendMetaType(clsid, m_typeDesc.GetTypeData());
				for (const ibClassID& member : members)
					m_typeValueDesc.AppendMetaType(member, m_typeDesc.GetTypeData());
				replaced = replaced || !members.empty();
			}
			if (!replaced)
				m_typeValueDesc = ibTypeDescription();

			m_object_version = object_version;
		}

		if (!m_typeDesc.IsOk()) SetDefaultMetaType();
	}
}