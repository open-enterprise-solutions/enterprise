////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-tables
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"
#include "metadata/metadata.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaTableObject, IMetaObject)

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

CMetaTableObject::CMetaTableObject() : IMetaObject()
{
	m_numberLine = CMetaDefaultAttributeObject::CreateNumber("numberLine", _("N"), wxEmptyString, 6, 0);
	m_numberLine->SetClsid(g_metaDefaultAttributeCLSID);

	//set child/parent
	m_numberLine->SetParent(this);
	AddChild(m_numberLine);
}

CMetaTableObject::~CMetaTableObject()
{
	wxDELETE(m_numberLine);
}

bool CMetaTableObject::LoadData(CMemoryReader &dataReader)
{
	//load default attributes:
	m_numberLine->LoadMeta(dataReader);
	return true;
}

bool CMetaTableObject::SaveData(CMemoryWriter &dataWritter)
{
	//save default attributes:
	m_numberLine->SaveMeta(dataWritter);
	return true;
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool CMetaTableObject::OnCreateMetaObject(IMetadata *metaData)
{
	if (!IMetaObject::OnCreateMetaObject(metaData))
		return false;

	if (!m_numberLine->OnCreateMetaObject(metaData)) {
		return false;
	}

	IMetaObject *metaObject = GetParent();
	wxASSERT(metaObject);
	if (!metaObject->OnReloadMetaObject()) {
		return false;
	}

	return true;
}

bool CMetaTableObject::OnLoadMetaObject(IMetadata *metaData)
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
	IMetaObject *metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject()) {
		return IMetaObject::OnReloadMetaObject();
	}

	return false;
}

#include "metadata/singleMetaTypes.h"
#include "metadata/objects/baseObject.h"

bool CMetaTableObject::OnRunMetaObject(int flags)
{
	if (!m_numberLine->OnRunMetaObject(flags))
		return false;

	IMetaObjectValue *metaObject = wxStaticCast(
		GetParent(), IMetaObjectValue);
	wxASSERT(metaObject);
	if (metaObject) {
		registerTabularSection();
	}
	return IMetaObject::OnRunMetaObject(flags);
}

bool CMetaTableObject::OnCloseMetaObject()
{
	if (!m_numberLine->OnCloseMetaObject())
		return false;

	IMetaObjectValue *metaObject = wxStaticCast(
		GetParent(), IMetaObjectValue);
	wxASSERT(metaObject);
	if (metaObject) {
		unregisterTabularSection();
	}
	return IMetaObject::OnCloseMetaObject();
}

//***********************************************************************
//*                          Read&save property                         *
//***********************************************************************

void CMetaTableObject::ReadProperty()
{
	IMetaObject::ReadProperty();
}

void CMetaTableObject::SaveProperty()
{
	IMetaObject::SaveProperty();
}

//***********************************************************************
//*                           System metadata                           *
//***********************************************************************

std::vector<IMetaAttributeObject *> CMetaTableObject::GetObjectAttributes() const
{
	std::vector<IMetaAttributeObject *> tableAttributes;
	tableAttributes.push_back(m_numberLine);

	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID)
			tableAttributes.push_back(dynamic_cast<CMetaAttributeObject *>(metaObject));
	}

	return tableAttributes;
}

IMetaAttributeObject *CMetaTableObject::FindAttribute(meta_identifier_t id) const
{
	for (auto metaObject : m_aMetaObjects) {
		if (metaObject->GetClsid() == g_metaAttributeCLSID &&
			metaObject->GetMetaID() == id) {
			return dynamic_cast<CMetaAttributeObject *>(metaObject);
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

	for (auto attribute : m_aMetaObjects) {
		m_attributes.Add(attribute->GetName());
	}

	return m_attributes;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaTableObject, "tabularSection", g_metaTableCLSID);