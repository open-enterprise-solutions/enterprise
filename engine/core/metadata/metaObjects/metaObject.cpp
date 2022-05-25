////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "appData.h"
#include "compiler/methods.h"
#include "metadata/metadata.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObject, CValue)

//*****************************************************************************************
//*                                  MetaObject                                           *
//*****************************************************************************************

IMetaObject::IMetaObject(const wxString& name, const wxString& synonym, const wxString& comment) : CValue(eValueTypes::TYPE_VALUE, true), IObjectBase(), m_methods(new CMethods()), m_metaData(NULL),
m_metaName(name), m_metaSynonym(synonym), m_metaComment(comment), m_metaFlags(metaDefaultFlag), m_metaId(0)
{
	PropertyContainer* commonCategory = IObjectBase::CreatePropertyContainer("Common");
	commonCategory->AddProperty("name", PropertyType::PT_WXNAME);
	commonCategory->AddProperty("synonym", PropertyType::PT_WXSTRING);
	commonCategory->AddProperty("comment", PropertyType::PT_WXSTRING);

	m_category->AddCategory(commonCategory);
}

IMetaObject::~IMetaObject()
{
	wxDELETE(m_methods);
}

void IMetaObject::ReadProperty()
{
	IObjectBase::SetPropertyValue("name", m_metaName);
	IObjectBase::SetPropertyValue("synonym", m_metaSynonym);
	IObjectBase::SetPropertyValue("comment", m_metaComment);
}

void IMetaObject::SaveProperty()
{
	IObjectBase::GetPropertyValue("name", m_metaName);
	IObjectBase::GetPropertyValue("synonym", m_metaSynonym);
	IObjectBase::GetPropertyValue("comment", m_metaComment);
}

bool IMetaObject::LoadMeta(CMemoryReader& dataReader)
{
	//Save meta version 
	const version_identifier_t &version = dataReader.r_u32(); //reserved 

	//Load unique guid 
	wxString strGuid;
	dataReader.r_stringZ(strGuid);
	m_metaGuid = strGuid;

	//Load meta id
	m_metaId = dataReader.r_u32();

	//Load standart fields
	dataReader.r_stringZ(m_metaName);
	dataReader.r_stringZ(m_metaSynonym);
	dataReader.r_stringZ(m_metaComment);

	//special info deleted 
	if (dataReader.r_u8()) {
		MarkAsDeleted();
	}

	return LoadData(dataReader);
}

bool IMetaObject::SaveMeta(CMemoryWriter& dataWritter)
{
	//Save meta version 
	dataWritter.w_u32(version_oes_last); //reserved 

	//Save unique guid
	dataWritter.w_stringZ(m_metaGuid);

	//Save meta id 
	dataWritter.w_u32(m_metaId);

	//Save standart fields
	dataWritter.w_stringZ(m_metaName);
	dataWritter.w_stringZ(m_metaSynonym);
	dataWritter.w_stringZ(m_metaComment);

	//special info deleted
	dataWritter.w_u8(IsDeleted());

	return SaveData(dataWritter);
}

bool IMetaObject::LoadMetaObject(IMetadata* metaData, CMemoryReader& dataReader)
{
	m_metaData = metaData;

	if (!LoadMeta(dataReader))
		return false;

	if (!OnLoadMetaObject(metaData))
		return false;

	return true;
}

bool IMetaObject::SaveMetaObject(IMetadata* metaData, CMemoryWriter& dataWritter, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	if (m_metaData != metaData) {
		return false;
	}

	if (!SaveMeta(dataWritter)) {
		return false;
	}

	if (!saveToFile &&
		!OnSaveMetaObject()) {
		return false;
	}

	return true;
}

bool IMetaObject::DeleteMetaObject(IMetadata* metaData)
{
	if (m_metaData != metaData) {
		return false;
	}

	if (!DeleteData()) {
		return false;
	}

	return true;
}

bool IMetaObject::CreateMetaTable(IConfigMetadata* srcMetaData)
{
	return CreateAndUpdateTableDB(srcMetaData, NULL, createMetaTable);
}

bool IMetaObject::UpdateMetaTable(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject)
{
	return CreateAndUpdateTableDB(srcMetaData, srcMetaObject, updateMetaTable);
}

bool IMetaObject::DeleteMetaTable(IConfigMetadata* srcMetaData)
{
	return CreateAndUpdateTableDB(srcMetaData, NULL, deleteMetaTable);
}

bool IMetaObject::OnCreateMetaObject(IMetadata* metaData)
{
	GenerateGuid();
	wxASSERT(metaData);
	m_metaId = metaData->GenerateNewID();
	m_metaData = metaData;
#ifdef _DEBUG  
	wxLogDebug("* Create metadata object %s with id %i", 
		GetClassName(), GetMetaID()
	);
#endif
	return true;
}

bool IMetaObject::OnLoadMetaObject(IMetadata* metaData)
{
	m_metaData = metaData;
	return true;
}

bool IMetaObject::OnDeleteMetaObject()
{
	return true;
}

bool IMetaObject::OnAfterCloseMetaObject()
{
	IMetadataTree* metaTree =
		m_metaData->GetMetaTree();

	if (metaTree) {
		metaTree->CloseMetaObject(this);
	}

	return true;
}

IMetadata* IMetaObject::GetMetaData() const
{
	return GetMetadata();
}

OptionList* IMetaObject::GetTypelist() const
{
	return m_metaData->GetTypelist();
}

wxString IMetaObject::GetModuleName() const
{
	return GetClassName() + wxT(": ") + m_metaName;
}

wxString IMetaObject::GetFullName() const
{
	wxString m_metaFullName = GetModuleName();
	IMetaObject* metaParent = GetParent();

	while (metaParent)
	{
		if (g_metaCommonMetadataCLSID == metaParent->GetClsid())
			break;

		wxString m_sModuleName = metaParent->GetName();
		m_metaFullName = m_sModuleName + '.' + m_metaFullName;
		metaParent = metaParent->GetParent();
	}

	return m_metaFullName;
}

wxString IMetaObject::GetFileName() const
{
	return m_metaData->GetFileName();
}

wxString IMetaObject::GetDocPath() const
{
	return m_metaGuid.str();
}