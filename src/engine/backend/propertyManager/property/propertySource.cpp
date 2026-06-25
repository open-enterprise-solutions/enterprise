#include "propertySource.h"
#include "backend/propertyManager/property/variant/variantSource.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)
#include "backend/sourceDescription.h"
#include "backend/metaCollection/partial/commonObject.h"   // ibSourceDataObject + path walk

wxObject* (*ibPropertySource::ms_propertySource)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;

////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibValueTypes& type) const
{
	return CreateVariantData(property, ibTypeDescription(ibValue::GetIDByVT(type)));
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibClassID& clsid) const
{
	return CreateVariantData(property, ibTypeDescription(clsid));
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibTypeDescription& typeDesc) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, typeDesc);
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibMetaID& id) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, id);
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibGuid& id, bool fillTypeDesc) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, id, fillTypeDesc);
}

wxVariantData* ibPropertySource::CreateVariantData(const ibPropertyObject* property, const ibSourceDescription& desc) const
{
	const ibBackendTypeSourceFactory* propFactory = dynamic_cast<const ibBackendTypeSourceFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataSource(propFactory, desc);
}

////////////////////////////////////////////////////////////////////////
ibMetaID ibPropertySource::GetValueAsSource() const { return get_cell_variant<ibVariantDataSource>()->GetSource(); }
ibGuid ibPropertySource::GetValueAsSourceGuid() const { return get_cell_variant<ibVariantDataSource>()->GetSourceGuid(); }
ibTypeDescription& ibPropertySource::GetValueAsTypeDesc(bool fillTypeDesc) const { return get_cell_variant<ibVariantDataSource>()->GetSourceTypeDesc(fillTypeDesc); }
void ibPropertySource::SetValue(const ibMetaID& val) { m_propValue = CreateVariantData(m_owner, val); }
void ibPropertySource::SetValue(const ibGuid& val, bool fillTypeDesc) { m_propValue = CreateVariantData(m_owner, val, fillTypeDesc); }
void ibPropertySource::SetValue(const ibTypeDescription& val) { m_propValue = CreateVariantData(m_owner, val); }
void ibPropertySource::SetValue(const ibSourceDescription& val) { m_propValue = CreateVariantData(m_owner, val); }
////////////////////////////////////////////////////////////////////////

ibSourceDescription& ibPropertySource::GetValueAsSourceDesc() const { return get_cell_variant<ibVariantDataSource>()->GetSourceDesc(); }
const std::vector<ibSourceId>& ibPropertySource::GetValueAsPath() const { return get_cell_variant<ibVariantDataSource>()->GetSourceDesc().GetPath(); }
////////////////////////////////////////////////////////////////////////

bool ibPropertySource::IsDotWalk() const
{
	// Cheap: the variant just measures the stored path length.
	return get_cell_variant<ibVariantDataSource>()->IsDotWalk();
}
////////////////////////////////////////////////////////////////////////

const ibBackendSourceColumn* ibPropertySource::GetSourceAttributeObject() const {
	return get_cell_variant<ibVariantDataSource>()->GetSourceAttributeObject();
}

std::vector<ibBackendFormAttributeValue*> ibPropertySource::GetSourceList() const {
	// Via the variant (which holds the owning control's type factory) — same path
	// as GetSourceAttributeObject; no dependency on the frontend control type.
	std::vector<ibBackendFormAttributeValue*> out;
	get_cell_variant<ibVariantDataSource>()->GetSourceList(out);
	return out;
}

////////////////////////////////////////////////////////////////////////

bool ibPropertySource::IsEmptyProperty() const {
	return get_cell_variant<ibVariantDataSource>()->IsEmptySource();
}

////////////////////////////////////////////////////////////////////////

//base property for "source"
bool ibPropertySource::SetDataValue(const ibValue& varPropVal)
{
	//varPropVal.GetString();
	return false;
}

bool ibPropertySource::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = m_propValue.GetString();
	return true;
}

// NB: go through a CONST owner pointer. ibPropertyObject has two virtual GetMetaData
// overloads (const + non-const); a control overrides only the CONST one (the form-based
// metaData) — the non-const base overload still returns nullptr. A non-const m_owner would
// pick the null one, so the source binding would serialize against a null metaData and the
// path GUIDs would all come out null. Selecting the const overload resolves them correctly.
bool ibPropertySource::ReadNodeValue(const ibDataValue& value)
{
	const ibPropertyObject* owner = m_owner;
	return ibSourceDescriptionMemory::ReadNode(value,
		GetValueAsSourceDesc(), owner->GetMetaData());
}

bool ibPropertySource::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;
	return ibSourceDescriptionMemory::WriteNode(value,
		GetValueAsSourceDesc(), owner->GetMetaData());
}
