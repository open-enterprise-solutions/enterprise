////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-tables
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"
#include "core/metadata/metadata.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaTableObject, IMetaObject)

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "core/metadata/singleClass.h"
#include "core/metadata/metaObjects/objects/object.h"

typeDescription_t CMetaTableObject::GetTypeDescription() const
{
	IMetaTypeObjectValueSingle* singleObject =
		m_metaData->GetTypeObject(this, eMetaObjectType::enTabularSection);
	wxASSERT(singleObject);
	if (singleObject != NULL)
		return typeDescription_t(singleObject->GetTypeClass());
	return typeDescription_t();
}

////////////////////////////////////////////////////////////////////////////

CMetaTableObject::CMetaTableObject() : IMetaObject()
{
	m_numberLine = CMetaDefaultAttributeObject::CreateNumber("numberLine", _("N"), wxEmptyString, 6, 0);
	//set child/parent
	m_numberLine->SetParent(this);
	AddChild(m_numberLine);
}

CMetaTableObject::~CMetaTableObject()
{
	wxDELETE(m_numberLine);
}

bool CMetaTableObject::LoadData(CMemoryReader& dataReader)
{
	m_propertyUse->SetValue(dataReader.r_u16());

	//load default attributes:
	m_numberLine->LoadMeta(dataReader);
	return true;
}

bool CMetaTableObject::SaveData(CMemoryWriter& dataWritter)
{
	dataWritter.w_u16(m_propertyUse->GetValueAsInteger());
	
	//save default attributes:
	m_numberLine->SaveMeta(dataWritter);
	return true;
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool CMetaTableObject::OnCreateMetaObject(IMetadata* metaData)
{
	if (!IMetaObject::OnCreateMetaObject(metaData))
		return false;

	if (!m_numberLine->OnCreateMetaObject(metaData)) {
		return false;
	}

	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (!metaObject->OnReloadMetaObject()) {
		return false;
	}

	return true;
}

bool CMetaTableObject::OnLoadMetaObject(IMetadata* metaData)
{
	if (!m_numberLine->OnLoadMetaObject(metaData))
		return false;

	return IMetaObject::OnLoadMetaObject(metaData);
}

bool CMetaTableObject::OnSaveMetaObject()
{
	if (!m_numberLine->OnDeleteMetaObject())
		return false;

	return IMetaObject::OnSaveMetaObject();
}

bool CMetaTableObject::OnDeleteMetaObject()
{
	if (!m_numberLine->OnDeleteMetaObject())
		return false;

	return IMetaObject::OnDeleteMetaObject();
}

bool CMetaTableObject::OnReloadMetaObject()
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject()) {
		return IMetaObject::OnReloadMetaObject();
	}

	return false;
}

bool CMetaTableObject::OnBeforeRunMetaObject(int flags)
{
	if (!m_numberLine->OnBeforeRunMetaObject(flags))
		return false;

	IMetaObjectRecordData* metaObject = wxStaticCast(
		GetParent(), IMetaObjectRecordData);
	wxASSERT(metaObject);
	if (metaObject != NULL) {
		registerTabularSection();
	}

	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaTableObject::OnAfterCloseMetaObject()
{
	if (!m_numberLine->OnAfterCloseMetaObject())
		return false;

	IMetaObjectRecordData* metaObject = wxStaticCast(
		GetParent(), IMetaObjectRecordData);
	wxASSERT(metaObject);
	if (metaObject) {
		unregisterTabularSection();
	}
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           System metadata                           *
//***********************************************************************

std::vector<IMetaAttributeObject*> CMetaTableObject::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject*> tableAttributes;
	tableAttributes.push_back(m_numberLine);

	for (auto metaObject : m_metaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID) {
			tableAttributes.push_back(
				dynamic_cast<IMetaAttributeObject*>(metaObject)
			);
		}
	}

	return tableAttributes;
}

IMetaAttributeObject* CMetaTableObject::FindProp(const meta_identifier_t& id) const
{
	for (auto metaObject : m_metaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID && metaObject->GetMetaID() == id) {
			return dynamic_cast<IMetaAttributeObject*>(metaObject);
		}
	}

	if (m_numberLine->GetMetaID() == id) {
		return m_numberLine;
	}

	return NULL;
}

wxArrayString CMetaTableObject::GetAttributes()
{
	wxArrayString m_attributes;

	for (auto attribute : m_metaObjects) {
		m_attributes.Add(attribute->GetName());
	}

	return m_attributes;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaTableObject, "tabularSection", g_metaTableCLSID);