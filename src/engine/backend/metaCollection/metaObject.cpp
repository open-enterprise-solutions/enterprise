////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/appData.h"

#include "backend/metaData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObject, CValue)

//*****************************************************************************************
//*                                  MetaObject                                           *
//*****************************************************************************************

void IMetaObject::AddRole(Role* role)
{
	m_roles.emplace_back(role->GetName(), role);
}

bool IMetaObject::AccessRight(const Role* role, const meta_identifier_t& id) const
{
	auto roleData = m_valRoles.find(id);
	if (roleData != m_valRoles.end()) {
		auto foundedData = std::find_if(roleData->second.begin(), roleData->second.end(), [role](const std::pair<wxString, bool >& pair) {
			return stringUtils::CompareString(role->GetName(), pair.first);
			}
		);
		if (foundedData != roleData->second.end())
			return foundedData->second;
	}

	return role->GetDefValue();
}

bool IMetaObject::SetRight(const Role* role, const meta_identifier_t& id, const bool& val)
{
	if (role == nullptr)
		return false;
	m_valRoles[id].insert_or_assign(role->GetName(), val);
	m_metaData->Modify(true);
	return true;
}
Role* IMetaObject::GetRole(const wxString& nameParam) const
{
	auto it = std::find_if(m_roles.begin(), m_roles.end(),
		[nameParam](const std::pair<wxString, Role*>& pair) {
			return stringUtils::CompareString(nameParam, pair.first);
		}
	);

	if (it != m_roles.end())
		return it->second;

	return nullptr;
}

Role* IMetaObject::GetRole(unsigned int idx) const
{
	assert(idx < m_roles.size());

	auto it = m_roles.begin();
	unsigned int i = 0;
	while (i < idx && it != m_roles.end()) {
		i++;
		it++;
	}

	if (it != m_roles.end())
		return it->second;
	return nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////

void IMetaObject::ResetGuid()
{
	m_metaGuid = wxNewUniqueGuid;
}

void IMetaObject::ResetId()
{
	if (m_metaData != nullptr) {
		m_metaId = m_metaData->GenerateNewID();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

IBackendMetadataTree* IMetaObject::GetMetaDataTree() const
{
	return m_metaData ? m_metaData->GetMetaTree() : nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////

bool IMetaObject::BuildNewName()
{
	bool foundedName = false;
	for (auto& obj : m_metaData->GetMetaObject(GetClassType())) {
		if (obj->GetParent() != GetParent())
			continue;
		if (obj != this &&
			stringUtils::CompareString(GetName(), obj->GetName())) {
			foundedName = true;
			break;
		}
	}


	if (foundedName) {
		const wxString& metaOldName = m_propertyName->GetValueAsString();
		bool isDigit = metaOldName.Length() > 0 ?
			stringUtils::IsDigit(metaOldName[metaOldName.Length() - 1]) : false;
		const wxString& metaName = m_metaData->GetNewName(GetClassType(), GetParent(), isDigit ? metaOldName.Left(metaOldName.Length() - 1) : metaOldName);
		SetName(metaName);
		const wxString& metaOldSynonym = m_propertySynonym->GetValueAsString();
		const wxString& metaSynonym = metaOldSynonym.Length() > 0 ? stringUtils::GenerateSynonym(metaName) : wxEmptyString;
		SetSynonym(metaSynonym);
	}

	return !foundedName;
}

/////////////////////////////////////////////////////////////////////////////////////////

IMetaObject::IMetaObject(const wxString& strName, const wxString& synonym, const wxString& comment) : CValue(eValueTypes::TYPE_VALUE, true), IPropertyObject(),
m_methodHelper(new CMethodHelper()), m_metaData(nullptr), m_metaFlags(metaDefaultFlag), m_metaId(0)
{
	m_propertyName->SetValue(strName);
	m_propertySynonym->SetValue(synonym);
	m_propertyComment->SetValue(comment);
}

IMetaObject::~IMetaObject()
{
	for (auto& role : m_roles) {
		wxDELETE(role.second);
	}
	m_roles.clear();
	wxDELETE(m_methodHelper);
}

bool IMetaObject::LoadRole(CMemoryReader& dataReader)
{
	unsigned int countRole = dataReader.r_u32(); m_valRoles.clear();
	for (unsigned int idx = 0; idx < countRole; idx++) {
		unsigned int countData = dataReader.r_u32();
		meta_identifier_t id = dataReader.r_s32();
		for (unsigned int idc = 0; idc < countData; idc++) {
			wxString roleName = dataReader.r_stringZ();
			bool roleValue = dataReader.r_u8();
			m_valRoles[id].insert_or_assign(roleName, roleValue);
		}
	}
	return true;
}

bool IMetaObject::LoadMeta(CMemoryReader& dataReader)
{
	//Save meta version 
	const version_identifier_t& version = dataReader.r_u32(); //reserved 

	//Load unique guid 
	wxString strGuid;
	dataReader.r_stringZ(strGuid);
	m_metaGuid = strGuid;

	//Load meta id
	m_metaId = dataReader.r_u32();

	//Load standart fields
	m_propertyName->SetValue(dataReader.r_stringZ());
	m_propertySynonym->SetValue(dataReader.r_stringZ());
	m_propertyComment->SetValue(dataReader.r_stringZ());

	//special info deleted 
	if (dataReader.r_u8()) {
		MarkAsDeleted();
	}

	//load roles 
	if (!LoadRole(dataReader)) {
		return false;
	}

	return LoadData(dataReader);
}

bool IMetaObject::SaveRole(CMemoryWriter& dataWritter)
{
	dataWritter.w_u32(m_valRoles.size());
	for (auto role_id : m_valRoles) {
		dataWritter.w_u32(role_id.second.size());
		dataWritter.w_s32(role_id.first); // role id
		for (auto role_data : role_id.second) {
			dataWritter.w_stringZ(role_data.first); //strName role
			dataWritter.w_u8(role_data.second); // value true/false
		}
	}

	return true;
}

#define propBlock 0x00023456
#define eventBlock 0x00023457

bool IMetaObject::ReadProperty(CMemoryReader& reader)
{
	std::shared_ptr <CMemoryReader>propReader(reader.open_chunk(propBlock));
	if (propReader != nullptr) {
		for (u64 iter_pos = 0; ; iter_pos++) {
			std::shared_ptr <CMemoryReader>propDataReader(propReader->open_chunk(iter_pos));
			if (propDataReader == nullptr)
				break;
			Property* prop = GetProperty(propDataReader->r_stringZ());
			if (prop != nullptr && !prop->LoadData(*propDataReader))
				return false;
		}
	}
	std::shared_ptr <CMemoryReader>eventReader(reader.open_chunk(eventBlock));
	if (eventReader != nullptr) {
		for (u64 iter_pos = 0; ; iter_pos++) {
			std::shared_ptr <CMemoryReader>eventDataReader(eventReader->open_chunk(iter_pos));
			if (eventDataReader == nullptr)
				break;
			Event* event = GetEvent(eventDataReader->r_stringZ());
			if (event != nullptr && !event->LoadData(*eventDataReader))
				return false;
		};
	}
	return true;
}


bool IMetaObject::SaveProperty(CMemoryWriter& writter) const
{
	CMemoryWriter propWritter;
	for (unsigned int idx = 0; idx < GetPropertyCount(); idx++) {
		Property* prop = GetProperty(idx);
		wxASSERT(prop);
		CMemoryWriter propDataWritter;
		propDataWritter.w_stringZ(prop->GetName());
		if (!prop->SaveData(propDataWritter))
			return false;
		propWritter.w_chunk(idx, propDataWritter.pointer(), propDataWritter.size());
	}

	writter.w_chunk(propBlock, propWritter.pointer(), propWritter.size());

	CMemoryWriter eventWritter;
	for (unsigned int idx = 0; idx < GetEventCount(); idx++) {
		Event* event = GetEvent(idx);
		wxASSERT(event);
		CMemoryWriter eventDataWritter;
		eventDataWritter.w_stringZ(event->GetName());
		if (!event->SaveData(eventDataWritter))
			return false;
		eventWritter.w_chunk(idx, eventDataWritter.pointer(), eventDataWritter.size());
	}

	writter.w_chunk(eventBlock, eventWritter.pointer(), eventWritter.size());
	return true;
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
	dataWritter.w_stringZ(m_propertyName->GetValueAsString());
	dataWritter.w_stringZ(m_propertySynonym->GetValueAsString());
	dataWritter.w_stringZ(m_propertyComment->GetValueAsString());

	//special info deleted
	dataWritter.w_u8(IsDeleted());

	//save roles 
	if (!SaveRole(dataWritter)) {
		return false;
	}

	return SaveData(dataWritter);
}

bool IMetaObject::LoadMetaObject(IMetaData* metaData, CMemoryReader& dataReader)
{
	m_metaData = metaData;

	if (!LoadMeta(dataReader))
		return false;

	if (!OnLoadMetaObject(metaData))
		return false;

	return true;
}

bool IMetaObject::SaveMetaObject(IMetaData* metaData, CMemoryWriter& dataWritter, int flags)
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

bool IMetaObject::DeleteMetaObject(IMetaData* metaData)
{
	if (m_metaData != metaData) {
		return false;
	}

	if (!DeleteData()) {
		return false;
	}

	return true;
}

bool IMetaObject::CreateMetaTable(IMetaDataConfiguration* srcMetaData)
{
	return CreateAndUpdateTableDB(srcMetaData, nullptr, createMetaTable);
}

bool IMetaObject::UpdateMetaTable(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject)
{
	return CreateAndUpdateTableDB(srcMetaData, srcMetaObject, updateMetaTable);
}

bool IMetaObject::DeleteMetaTable(IMetaDataConfiguration* srcMetaData)
{
	return CreateAndUpdateTableDB(srcMetaData, this, deleteMetaTable);
}

bool IMetaObject::OnCreateMetaObject(IMetaData* metaData)
{
	GenerateGuid();
	wxASSERT(metaData);
	m_metaId = metaData->GenerateNewID();
	m_metaData = metaData;
#ifdef DEBUG  
	wxLogDebug("* Create metaData object %s with id %i",
		GetClassName(), GetMetaID()
	);
#endif
	return true;
}

bool IMetaObject::OnLoadMetaObject(IMetaData* metaData)
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
	IBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	if (metaTree != nullptr) 
		metaTree->CloseMetaObject(this);
	return true;
}

#define	headerBlock 0x002330
#define	dataBlock 0x002350
#define	childBlock 0x002370

bool IMetaObject::Init()
{
	// always false
	return false;
}

bool IMetaObject::Init(CValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	IMetaObject* metaParent = nullptr;
	if (paParams[0]->ConvertToValue(metaParent)) {
		const class_identifier_t& clsid = GetClassType();
		if (metaParent != nullptr) {
			SetParent(metaParent);
			metaParent->AddChild(this);
		}
		SetReadOnly(metaParent != nullptr ? metaParent->IsEditable() : true);
		return metaParent != nullptr ?
			metaParent->AppendChild(this) : true;
	}

	return false;
}

bool IMetaObject::CopyObject(CMemoryWriter& writer) const
{
	CMemoryWriter writterHeaderMemory;
	writterHeaderMemory.w_s32(m_metaData->GetVersion());
	writer.w_chunk(headerBlock, writterHeaderMemory.pointer(), writterHeaderMemory.size());
	CMemoryWriter writterDataMemory;
	if (!SaveProperty(writterDataMemory))
		return false;
	writer.w_chunk(dataBlock, writterDataMemory.pointer(), writterDataMemory.size());
	CMemoryWriter writterChildMemory;
	for (auto& obj : GetObjects()) {
		if (obj->IsDeleted())
			continue;
		CMemoryWriter writterMemory;
		if (!obj->CopyObject(writterMemory))
			return false;
		writterChildMemory.w_chunk(obj->GetClassType(), writterMemory.pointer(), writterMemory.size());
	}
	writer.w_chunk(childBlock, writterChildMemory.pointer(), writterChildMemory.size());
	return true;
}

bool IMetaObject::PasteObject(CMemoryReader& reader)
{
	std::shared_ptr <CMemoryReader>readerHeaderMemory(reader.open_chunk(headerBlock));
	version_identifier_t version = readerHeaderMemory->r_s32();
	std::shared_ptr <CMemoryReader>readerDataMemory(reader.open_chunk(dataBlock));
	if (!ReadProperty(*readerDataMemory))
		return false;
	BuildNewName();
	std::shared_ptr <CMemoryReader> readerChildMemory(reader.open_chunk(childBlock));
	if (readerChildMemory != nullptr) {
		CMemoryReader* prevReaderMemory = nullptr;
		do {
			class_identifier_t clsid = 0;
			CMemoryReader* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
			if (readerMemory == nullptr)
				break;
			if (clsid > 0) {
				IMetaObject* metaObject = m_metaData->CreateMetaObject(clsid, this);
				if (metaObject != nullptr && !metaObject->PasteObject(*readerMemory))
					return false;
			}
			prevReaderMemory = readerMemory;
		} while (true);
	}

	return true;
}

bool IMetaObject::ChangeChildPosition(IPropertyObject* obj, unsigned int pos)
{
	unsigned int obj_pos = GetChildPosition(obj);
	if (obj_pos == GetChildCount() || pos >= GetChildCount())
		return false;
	if (pos == obj_pos)
		return true;
	auto dst = std::find(m_listMetaObject.begin(), m_listMetaObject.end(), GetChild(pos));
	auto src = std::find(m_listMetaObject.begin(), m_listMetaObject.end(), obj);
	std::swap(*dst, *src);
	m_metaData->Modify(true);
	return IPropertyObject::ChangeChildPosition(obj, pos);
}

wxString IMetaObject::GetModuleName() const
{
	IMetaObject* metaParent = GetParent();
	wxASSERT(metaParent);
	if (metaParent != nullptr) {
		return metaParent->GetName() + wxT(": ") + GetName();
	}
	return GetName();
}

wxString IMetaObject::GetFullName() const
{
	wxString strFullName;
	
	IMetaObject* metaParent = GetParent();
	while (metaParent != nullptr) {	
		if (g_metaCommonMetadataCLSID != metaParent->GetClassType()) {
			const wxString& strModuleName = metaParent->GetName();
			strFullName = strModuleName + '.' + strFullName;
		}
		metaParent = metaParent->GetParent();
	}
	
	return strFullName + GetName();
}

wxString IMetaObject::GetFileName() const
{
	return m_metaData->GetFileName();
}

wxString IMetaObject::GetDocPath() const
{
	return m_metaGuid.str();
}

//****************************************************************************

IMetaObject* IMetaObject::FindByName(const class_identifier_t& clsid, const wxString& strDocPath) const
{
	for (auto& obj : m_listMetaObject) {
		if (strDocPath == obj->GetDocPath()) return obj;
	}
	return nullptr;
}

IMetaObject* IMetaObject::FindByName(const wxString& strDocPath) const
{
	for (auto& obj : m_listMetaObject) {
		if (strDocPath == obj->GetDocPath()) return obj;
	}
	return nullptr;
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void IMetaObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	for (unsigned idx = 0; idx < IPropertyObject::GetPropertyCount(); idx++) {
		Property* property = IPropertyObject::GetProperty(idx);
		if (property == nullptr)
			continue;
		m_methodHelper->AppendProp(property->GetName(), idx);
	}
}

bool IMetaObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	Property* property = GetPropertyByIndex(lPropNum);
	if (property != nullptr) {
		property->SetDataValue(varPropVal);
		return true;
	}
	return false;
}

bool IMetaObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	Property* property = GetPropertyByIndex(lPropNum);
	if (property != nullptr) {
		pvarPropVal = property->GetDataValue();
		return true;
	}
	return false;
}