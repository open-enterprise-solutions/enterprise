#include "variantSource.h"
#include "backend/metaData.h"

////////////////////////////////////////////////////////////////////////////

void ibVariantDataAttributeSource::DoSetFromMetaId(const ibMetaID& id)
{
	if (m_ownerSrcProperty != nullptr && id != wxNOT_FOUND) {
		const ibSourceObject* srcData = m_ownerSrcProperty->GetSourceObject();
		if (srcData != nullptr) {
			const ibValueMetaObjectCompositeData* metaObject = srcData->GetSourceMetaObject();
			if (metaObject != nullptr && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				m_typeDesc.SetDefaultMetaType(srcData->GetSourceClassType());
				return;
			}
		}
	}

	ibVariantDataAttribute::DoSetFromMetaId(id);
}

#include "backend/objCtor.h"

void ibVariantDataAttributeSource::DoRefreshTypeDesc()
{
	if (m_ownerSrcProperty != nullptr) {

		std::set<ibClassID> clear_list;

		const ibMetaData* metaData = m_ownerSrcProperty->GetMetaData();
		wxASSERT(metaData);
		const ibSourceObject* srcObject = m_ownerSrcProperty->GetSourceObject();

		for (auto clsid : m_typeDesc.GetClsidList()) {
			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
			if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType_TabularSection) {
				if (srcObject != nullptr) {
					const ibValueMetaObject* metaTable = typeCtor->GetMetaObject();
					if (metaTable == nullptr) clear_list.insert(clsid);
					else if (metaTable->GetParent() != srcObject->GetSourceMetaObject()) clear_list.insert(clsid);
				}
				else if (srcObject == nullptr)clear_list.insert(clsid);
			}
		}

		for (auto clsid : clear_list) { m_typeDesc.ClearMetaType(clsid); }
		if (!m_typeDesc.IsOk()) SetDefaultMetaType();
	}

	ibVariantDataAttribute::DoRefreshTypeDesc();
}

////////////////////////////////////////////////////////////////////////////

wxString ibVariantDataSource::MakeString() const
{
	if (!m_dataSource.isValid()) return _("<not selected>");
	if (m_ownerProperty != nullptr) {
		const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
		if (sourceObject != nullptr) {
			const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
			//wxASSERT(genericObject);
			const ibValueMetaObject* metaObject = genericObject != nullptr && genericObject->IsAllowed() ?
				genericObject->FindAnyObjectByFilter(m_dataSource) : nullptr;
			if (metaObject != nullptr && !metaObject->IsAllowed()) return _("<not selected>");
			else if (metaObject == nullptr) return _("<not selected>");
			return metaObject->GetName();
		}
	}
	return _("<not selected>");
}

////////////////////////////////////////////////////////////////////////////

ibMetaID ibVariantDataSource::GetIdByGuid(const ibGuid& guid) const
{
	const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
	if (guid.isValid() && sourceObject != nullptr) {
		const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
		//wxASSERT(genericObject);
		const ibValueMetaObject* metaObject = genericObject != nullptr && genericObject->IsAllowed() ?
			genericObject->FindAnyObjectByFilter(guid) : nullptr;
		//wxASSERT(metaObject);
		return metaObject != nullptr &&
			metaObject->IsAllowed() ? metaObject->GetMetaID() : wxNOT_FOUND;
	}
	return wxNOT_FOUND;
}

ibGuid ibVariantDataSource::GetGuidByID(const ibMetaID& id) const
{
	const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
	if (id != wxNOT_FOUND && sourceObject != nullptr) {
		const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
		//wxASSERT(objMetaValue);
		const ibValueMetaObject* metaObject = genericObject != nullptr && genericObject->IsAllowed() ?
			genericObject->FindAnyObjectByFilter(id) : nullptr;
		//wxASSERT(metaObject);
		return metaObject != nullptr && metaObject->IsAllowed() ? metaObject->GetCommonGuid() : wxNullGuid;

	}
	return wxNullGuid;
}

////////////////////////////////////////////////////////////////////////////

ibValueMetaObjectAttributeBase* ibVariantDataSource::GetSourceAttributeObject() const
{
	const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
	if (m_dataSource.isValid() && sourceObject != nullptr) {
		const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
		//wxASSERT(genericObject);
		return genericObject != nullptr && genericObject->IsAllowed() ?
			dynamic_cast<ibValueMetaObjectAttributeBase*>(genericObject->FindAnyObjectByFilter(m_dataSource)) : nullptr;
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::SetSource(const ibMetaID& id, bool fillTypeDesc)
{
	const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
	if (id != wxNOT_FOUND && sourceObject != nullptr) {
		const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
		//wxASSERT(genericObject);
		const ibValueMetaObject* metaObject = genericObject->FindAnyObjectByFilter(id);
		//wxASSERT(metaObject);
		m_dataSource = metaObject != nullptr && metaObject->IsAllowed() ?
			metaObject->GetGuid() : wxNullGuid;
	}
	else {
		m_dataSource.reset();
	}

	if (fillTypeDesc) m_attributeSource->SetFromMetaDesc(id);
}

ibMetaID ibVariantDataSource::GetSource() const
{
	return GetIdByGuid(m_dataSource);
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::SetSourceGuid(const ibGuid& guid, bool fillTypeDesc)
{
	const ibMetaID& id = GetIdByGuid(guid);
	m_dataSource = guid;
	if (fillTypeDesc) m_attributeSource->SetFromMetaDesc(id);
}

ibGuid ibVariantDataSource::GetSourceGuid() const
{
	const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
	if (m_dataSource.isValid() && sourceObject != nullptr) {
		const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
		if (genericObject == nullptr) return wxNullGuid;
		const ibValueMetaObject* metaObject = genericObject ? genericObject->FindAnyObjectByFilter(m_dataSource) : nullptr;
		//wxASSERT(metaObject);
		return metaObject != nullptr && metaObject->IsAllowed() ?
			metaObject->GetCommonGuid() : wxNullGuid;
	}
	return wxNullGuid;
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::SetSourceTypeDesc(const ibTypeDescription& td)
{
	ResetSource();
	m_attributeSource->SetFromTypeDesc(td);
}

ibTypeDescription& ibVariantDataSource::GetSourceTypeDesc(bool fillTypeDesc) const
{
	return m_attributeSource->GetTypeDesc();
}

////////////////////////////////////////////////////////////////////////////

void ibVariantDataSource::ResetSource()
{
	m_attributeSource->SetFromMetaDesc(wxNOT_FOUND);

	if (m_dataSource.isValid()) {
		wxASSERT(m_attributeSource->GetTypeDesc().GetClsidCount() > 0);
		m_dataSource.reset();
	}
}

////////////////////////////////////////////////////////////////////////////

bool ibVariantDataSource::IsPropAllowed() const
{
	const ibSourceObject* sourceObject = m_ownerProperty->GetSourceObject();
	if (m_dataSource.isValid() && sourceObject != nullptr) {
		const ibValueMetaObjectCompositeData* genericObject = sourceObject->GetSourceMetaObject();
		if (genericObject == nullptr) return true;
		const ibValueMetaObject* metaObject = genericObject ? genericObject->FindAnyObjectByFilter(m_dataSource) : nullptr;
		//wxASSERT(metaObject);
		if (metaObject != nullptr)
			return !metaObject->IsAllowed();
		return true;
	}
	return true;
}
