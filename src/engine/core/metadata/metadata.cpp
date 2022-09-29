////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metadata 
////////////////////////////////////////////////////////////////////////////

#include "metadata.h"
#include "metadata/metaObjects/objects/systemManager.h"
#include "compiler/enumFactory.h"
#include "compiler/systemObjects.h"
#include "compiler/debugger/debugServer.h"
#include "compiler/debugger/debugClient.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "appData.h"

IConfigMetadata* IConfigMetadata::s_instance = NULL;

IConfigMetadata* IConfigMetadata::Get()
{
	wxASSERT(s_instance);
	return s_instance;
}

bool IConfigMetadata::Initialize(eRunMode mode)
{
	if (!s_instance) {
		switch (mode)
		{
		case eRunMode::DESIGNER_MODE:
			s_instance = new CConfigStorageMetadata();
			break;
		default:
			s_instance = new CConfigMetadata();
			break;
		}
		return s_instance->CreateMetadata();
	}

	return false;
}

void IConfigMetadata::Destroy()
{
	debugServerDestroy();
	debugClientDestroy();
	enumFactoryDestroy();

	wxDELETE(s_instance);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

bool IConfigMetadata::CreateMetadata()
{
	return databaseLayer->IsOpen();
}

wxString IConfigMetadata::GetConfigSaveTableName()
{
	return wxT("CONFIG_SAVE");
}

wxString IConfigMetadata::GetConfigTableName()
{
	return wxT("CONFIG");
}

wxString IConfigMetadata::GetCompileDataTableName()
{
	return wxT("COMPILE_DATA");
}

wxString IConfigMetadata::GetUsersTableName()
{
	return wxT("USERS");
}

wxString IConfigMetadata::GetActiveUsersTableName()
{
	return wxT("ACTIVE_USERS");
}

wxString IConfigMetadata::GetConfigParamsTableName()
{
	return wxT("CONFIG_PARAMS");
}

#include "utils/stringUtils.h"

//**************************************************************************************************
//*                                          IMetadata											   *
//**************************************************************************************************

//ID's 
meta_identifier_t IMetadata::GenerateNewID() const
{
	IMetaObject* commonObject = GetCommonMetaObject();
	wxASSERT(commonObject);
	meta_identifier_t id = commonObject->GetMetaID() + 1;
	DoGenerateNewID(id, commonObject);
	return id;
}

void IMetadata::DoGenerateNewID(meta_identifier_t& id, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		meta_identifier_t newID = child->GetMetaID() + 1;
		if (newID > id) {
			id = newID;
		}
		DoGenerateNewID(id, child);
	}
}

IMetaObject* IMetadata::CreateMetaObject(const CLASS_ID& clsid, IMetaObject* parentMetaObject)
{
	wxASSERT(clsid != 0);
	wxString classType = CValue::GetNameObjectFromID(clsid);
	wxASSERT(classType.Length() > 0);

	IMetaObject* newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(classType);
	wxASSERT(newMetaObject);

	newMetaObject->SetClsid(clsid);
	newMetaObject->SetName(
		GetNewName(clsid, parentMetaObject, newMetaObject->GetClassName())
	);

	if (parentMetaObject != NULL) {
		newMetaObject->SetParent(parentMetaObject);
		parentMetaObject->AddChild(newMetaObject);
	}

	bool success = parentMetaObject != NULL ?
		parentMetaObject->AppendChild(newMetaObject) : true;

	//always create metabject
	success = success && newMetaObject->OnCreateMetaObject(this);

	//first initialization
	if (!success || !newMetaObject->OnLoadMetaObject(this)) {
		if (parentMetaObject != NULL) {
			parentMetaObject->RemoveChild(newMetaObject);
		}
		wxDELETE(newMetaObject);
		return NULL;
	}

	//and running initialization
	if (!success || !newMetaObject->OnBeforeRunMetaObject(metaNewObjectFlag)) {
		if (parentMetaObject != NULL) {
			parentMetaObject->RemoveChild(newMetaObject);
		}
		wxDELETE(newMetaObject);
		return NULL;
	}

	Modify(true);

	if (!success || !newMetaObject->OnAfterRunMetaObject(metaNewObjectFlag)) {
		if (parentMetaObject != NULL) {
			parentMetaObject->RemoveChild(newMetaObject);
		}
		wxDELETE(newMetaObject);
		return NULL;
	}

	if (!newMetaObject->Init()) {
		if (parentMetaObject != NULL) {
			parentMetaObject->RemoveChild(newMetaObject);
		}
		wxDELETE(newMetaObject);
		return NULL;
	}

	newMetaObject->IncrRef();
	return newMetaObject;
}

wxString IMetadata::GetNewName(const CLASS_ID& clsid, IMetaObject* metaParent, const wxString& prefix, bool forConstructor)
{
	unsigned int countRec = forConstructor ?
		0 : 1;

	wxString currPrefix = prefix.Length() > 0 ?
		prefix : wxT("newItem");

	wxString newName = forConstructor ?
		wxString::Format("%s", currPrefix) :
		wxString::Format("%s%d", currPrefix, countRec);

	while (forConstructor ||
		countRec > 0) {
		bool foundedName = false;

		if (metaParent) {
			for (auto obj : metaParent->GetObjects(clsid)) {
				if (obj->IsDeleted())
					continue;
				if (newName == obj->GetName()) {
					foundedName = true; break;
				}
			}
		}

		if (!foundedName)
			break;

		newName = wxString::Format("%s%d", currPrefix, ++countRec);
	}

	return newName;
}

std::vector<IMetaObject*> IMetadata::GetMetaObjects(const CLASS_ID& clsid) const
{
	std::vector<IMetaObject*> metaObjects;
	DoGetMetaObjects(clsid, metaObjects, GetCommonMetaObject());
	return metaObjects;
}

void IMetadata::DoGetMetaObjects(const CLASS_ID& clsid, std::vector<IMetaObject*>& metaObjects, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		DoGetMetaObjects(clsid, metaObjects, child);
	}

	if (top->IsDeleted())
		return;

	if (clsid == top->GetClsid()) {
		metaObjects.push_back(top);
	}
}

IMetaObject* IMetadata::FindByName(const wxString& fullName) const
{
	return DoFindByName(fullName, GetCommonMetaObject());
}

IMetaObject* IMetadata::DoFindByName(const wxString& fileName, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		IMetaObject* foundedMeta = DoFindByName(fileName, child);
		if (foundedMeta != NULL)
			return foundedMeta;
	}

	if (top->IsDeleted())
		return NULL;

	if (fileName == top->GetDocPath()) {
		return top;
	}

	return NULL;
}

IMetaObject* IMetadata::GetMetaObject(const meta_identifier_t& id, IMetaObject* top) const
{
	if (id == wxNOT_FOUND)
		return NULL;
	return DoGetMetaObject(id, top != NULL ? top : GetCommonMetaObject());
}

IMetaObject* IMetadata::GetMetaObject(const Guid& guid, IMetaObject* top) const
{
	if (!guid.isValid())
		return NULL;
	return DoGetMetaObject(guid, top != NULL ? top : GetCommonMetaObject());
}

IMetaObject* IMetadata::DoGetMetaObject(const meta_identifier_t& id, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		IMetaObject* foundedMeta = DoGetMetaObject(id, child);
		if (foundedMeta != NULL)
			return foundedMeta;
	}

	if (id == top->GetMetaID()) {
		return top;
	}

	return NULL;
}

IMetaObject* IMetadata::DoGetMetaObject(const Guid& guid, IMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		IMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		IMetaObject* foundedMeta = DoGetMetaObject(guid, child);
		if (foundedMeta != NULL)
			return foundedMeta;
	}

	if (guid == top->GetGuid()) {
		return top;
	}

	return NULL;
}

bool IMetadata::RenameMetaObject(IMetaObject* metaObject, const wxString& newName)
{
	bool foundedName = false;

	for (auto obj : GetMetaObjects(metaObject->GetClsid())) {
		if (obj->GetParent() != metaObject->GetParent())
			continue;
		if (obj != metaObject &&
			StringUtils::CompareString(newName, obj->GetName())) {
			foundedName = true;
			break;
		}
	}

	if (foundedName)
		return false;

	if (metaObject->OnRenameMetaObject(newName)) {
		Modify(true); return true;
	}

	return false;
}

void IMetadata::RemoveMetaObject(IMetaObject* metaObject, IMetaObject* parent)
{
	IMetaObject* objParent = parent ?
		parent : metaObject->GetParent();

	if (metaObject->OnAfterCloseMetaObject()) {
		if (metaObject->OnDeleteMetaObject()) {
			for (auto child : metaObject->GetObjects()) {
				RemoveMetaObject(child, metaObject);
			}
			metaObject->MarkAsDeleted();
		}

		metaObject->OnReloadMetaObject();
		Modify(true);
	}
}

#include "metadata/singleMetaTypes.h"

CValue* IMetadata::CreateObjectRef(const wxString& className, CValue** aParams)
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		CValue* newObject = singleObject->CreateObject();
		wxASSERT(newObject);
		if (newObject != NULL && !newObject->Init()) {
			if (!appData->DesignerMode()) {
				wxDELETE(newObject);
				CTranslateError::Error(_("Error initializing object '%s'"), className.wc_str());
			}
		}
		return newObject;
	}

	return CValue::CreateObjectRef(className, aParams);
}

void IMetadata::RegisterObject(const wxString& className, IMetaTypeObjectValueSingle* singleObject)
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className](IMetaTypeObjectValueSingle* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (IMetadata::IsRegisterObject(className)) {
		CTranslateError::Error(_("Object '%s' is exist"), className.wc_str());
	}

	if (IMetadata::IsRegisterObject(singleObject->GetClassType())) {
		CTranslateError::Error(_("Object '%s' is exist"), className.wc_str());
	}

	m_aFactoryMetaObjects.push_back(singleObject);
}

void IMetadata::UnRegisterObject(const wxString& className)
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className](IMetaTypeObjectValueSingle* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);

	if (itFounded != m_aFactoryMetaObjects.end()) {
		m_aFactoryMetaObjects.erase(itFounded);
	}

	delete singleObject;
}

bool IMetadata::IsRegisterObject(const wxString& className) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className](IMetaTypeObjectValueSingle* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded != m_aFactoryMetaObjects.end())
		return true;

	return CValue::IsRegisterObject(className);
}

bool IMetadata::IsRegisterObject(const wxString& className, eObjectType objectType) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className, objectType](IMetaTypeObjectValueSingle* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName())
			&& (objectType == singleObject->GetObjectType());
		});

	if (itFounded != m_aFactoryMetaObjects.end())
		return true;

	return CValue::IsRegisterObject(className, objectType);
}

bool IMetadata::IsRegisterObject(const wxString& className, eObjectType objectType, eMetaObjectType refType) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className, objectType, refType](IMetaTypeObjectValueSingle* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName())
			&& (objectType == singleObject->GetObjectType()
				&& refType == singleObject->GetMetaType());
		});

	if (itFounded != m_aFactoryMetaObjects.end())
		return true;

	return CValue::IsRegisterObject(className);
}

bool IMetadata::IsRegisterObject(const CLASS_ID& clsid) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [clsid](IMetaTypeObjectValueSingle* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded != m_aFactoryMetaObjects.end())
		return true;

	return CValue::IsRegisterObject(clsid);
}

CLASS_ID IMetadata::GetIDObjectFromString(const wxString& clsName) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [clsName](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(clsName, singleObject->GetClassName());
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		return singleObject->GetClassType();
	}

	return CValue::GetIDObjectFromString(clsName);
}

wxString IMetadata::GetNameObjectFromID(const CLASS_ID& clsid, bool upper) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		return upper ? singleObject->GetClassName().Upper() : singleObject->GetClassName();
	}

	return CValue::GetNameObjectFromID(clsid, upper);
}

meta_identifier_t IMetadata::GetVTByID(const CLASS_ID& clsid) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		IMetaTypeObjectValueSingle* singleObject = *itFounded;
		wxASSERT(singleObject);
		IMetaObject* metaValue = singleObject->GetMetaObject();
		wxASSERT(metaValue);
		return metaValue->GetMetaID();
	}

	return CValue::GetVTByID(clsid);
}

CLASS_ID IMetadata::GetIDByVT(const meta_identifier_t& valueType, eMetaObjectType refType) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [valueType, refType](IMetaTypeObjectValueSingle* singleObject) {
		IMetaObject* metaValue = singleObject->GetMetaObject();
		wxASSERT(metaValue);
		return refType == singleObject->GetMetaType() && valueType == metaValue->GetMetaID();
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		return singleObject->GetClassType();
	}

	return CValue::GetIDByVT((eValueTypes&)valueType);
}

IMetaTypeObjectValueSingle* IMetadata::GetTypeObject(const CLASS_ID& clsid) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [clsid](IMetaTypeObjectValueSingle* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		return *itFounded;
	}

	return NULL;
}

IMetaTypeObjectValueSingle* IMetadata::GetTypeObject(const IMetaObject* metaValue, eMetaObjectType refType) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [metaValue, refType](IMetaTypeObjectValueSingle* singleObject) {
		return refType == singleObject->GetMetaType() &&
			metaValue == singleObject->GetMetaObject();
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		return *itFounded;
	}

	return NULL;
}

wxArrayString IMetadata::GetAvailableObjects(eMetaObjectType refType) const
{
	wxArrayString classes;
	for (auto singleObject : m_aFactoryMetaObjects) {
		if (refType == singleObject->GetMetaType()) {
			classes.push_back(singleObject->GetClassName());
		}
	}
	classes.Sort();
	return classes;
}

IObjectValueAbstract* IMetadata::GetAvailableObject(const CLASS_ID& clsid) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [clsid](IMetaTypeObjectValueSingle* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		return *itFounded;
	}

	return CValue::GetAvailableObject(clsid);
}

IObjectValueAbstract* IMetadata::GetAvailableObject(const wxString& className) const
{
	auto itFounded = std::find_if(m_aFactoryMetaObjects.begin(), m_aFactoryMetaObjects.end(), [className](IMetaTypeObjectValueSingle* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded != m_aFactoryMetaObjects.end()) {
		return *itFounded;
	}

	return CValue::GetAvailableObject(className);
}

std::vector<IMetaTypeObjectValueSingle*> IMetadata::GetAvailableSingleObjects() const
{
	std::vector<IMetaTypeObjectValueSingle*> classes;

	for (auto singleObject : m_aFactoryMetaObjects) {
		classes.push_back(singleObject);
	}

	std::sort(classes.begin(), classes.end(),
		[](IMetaTypeObjectValueSingle* a, IMetaTypeObjectValueSingle* b) {
			IMetaObject* ma = a->GetMetaObject(); IMetaObject* mb = b->GetMetaObject();
			return ma->GetName() > mb->GetName() &&
				a->GetMetaType() > b->GetMetaType();
		});

	return classes;
}

std::vector<IMetaTypeObjectValueSingle*> IMetadata::GetAvailableSingleObjects(const CLASS_ID& clsid, eMetaObjectType refType) const
{
	std::vector<IMetaTypeObjectValueSingle*> classes;

	for (auto singleObject : m_aFactoryMetaObjects) {
		IMetaObject* metaObject = singleObject->GetMetaObject();
		if (refType == singleObject->GetMetaType()
			&& clsid == metaObject->GetClsid()) {
			classes.push_back(singleObject);
		}
	}

	std::sort(classes.begin(), classes.end(),
		[](IMetaTypeObjectValueSingle* a, IMetaTypeObjectValueSingle* b) {
			IMetaObject* ma = a->GetMetaObject(); IMetaObject* mb = b->GetMetaObject();
			return ma->GetName() > mb->GetName() &&
				a->GetMetaType() > b->GetMetaType();
		});

	return classes;
}

std::vector<IMetaTypeObjectValueSingle*> IMetadata::GetAvailableSingleObjects(eMetaObjectType refType) const
{
	std::vector<IMetaTypeObjectValueSingle*> classes;

	for (auto singleObject : m_aFactoryMetaObjects) {
		if (refType == singleObject->GetMetaType()) {
			classes.push_back(singleObject);
		}
	}

	std::sort(classes.begin(), classes.end(),
		[](IMetaTypeObjectValueSingle* a, IMetaTypeObjectValueSingle* b) {
			IMetaObject* ma = a->GetMetaObject(); IMetaObject* mb = b->GetMetaObject();
			return ma->GetName() > mb->GetName() &&
				a->GetMetaType() > b->GetMetaType();
		});

	return classes;
}

//**************************************************************************************************
//*                                          ConfigMetadata										   *
//**************************************************************************************************

CConfigFileMetadata::CConfigFileMetadata(bool readOnly) : IConfigMetadata(readOnly),
m_commonObject(NULL), m_configOpened(false)
{
	//create main metaObject
	m_commonObject = new CMetaObject();
	m_commonObject->SetClsid(g_metaCommonMetadataCLSID);

	if (m_commonObject->OnCreateMetaObject(this)) {
		m_moduleManager = new CModuleManager(this, m_commonObject);
		m_moduleManager->IncrRef();

		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
	}

	m_commonObject->IncrRef();
	wxASSERT(m_moduleManager);
}

CConfigFileMetadata::~CConfigFileMetadata()
{
	//clear data 
	if (!ClearMetadata()) {
		wxASSERT_MSG(false, "ClearMetadata() == false");
	}

	//delete module manager
	wxDELETE(m_moduleManager);
	//delete common metaObject
	wxDELETE(m_commonObject);
}

bool CConfigFileMetadata::RunMetadata(int flags)
{
	wxASSERT(!m_configOpened);

	if (!m_commonObject->OnBeforeRunMetaObject(flags)) {
		wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
		return false;
	}

	for (auto obj : m_commonObject->GetObjects()) {

		if (obj->IsDeleted())
			continue;

		if (!obj->OnBeforeRunMetaObject(flags))
			return false;

		if (!RunChildMetadata(obj, flags, true))
			return false;
	}

	if (m_moduleManager->CreateMainModule()) {

		if (!m_commonObject->OnAfterRunMetaObject(flags)) {
			wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
			return false;
		}

		for (auto obj : m_commonObject->GetObjects()) {

			if (obj->IsDeleted())
				continue;

			if (!obj->OnAfterRunMetaObject(flags))
				return false;

			if (!RunChildMetadata(obj, flags, false))
				return false;
		}

		if (!m_moduleManager->StartMainModule())
			return false;

		m_configOpened = true;
		return true;
	}

	return false;
}

bool CConfigFileMetadata::RunChildMetadata(IMetaObject* metaParent, int flags, bool before)
{
	for (auto obj : metaParent->GetObjects()) {

		if (obj->IsDeleted())
			continue;

		if (before && !obj->OnBeforeRunMetaObject(flags))
			return false;

		if (!before && !obj->OnAfterRunMetaObject(flags))
			return false;

		if (!RunChildMetadata(obj, flags, before))
			return false;
	}

	return true;
}

bool CConfigFileMetadata::CloseMetadata(int flags)
{
	wxASSERT(m_configOpened);

	for (auto obj : m_commonObject->GetObjects()) {

		if (obj->IsDeleted())
			continue;

		if (!obj->OnBeforeCloseMetaObject())
			return false;

		if (!CloseChildMetadata(obj, (flags & forceCloseFlag) != 0, true))
			return false;
	}

	if (!m_commonObject->OnBeforeCloseMetaObject()) {
		wxASSERT_MSG(false, "m_commonObject->OnAfterCloseMetaObject() == false");
		return false;
	}

	if (m_moduleManager->ExitMainModule((flags & forceCloseFlag) != 0)) {

		if (!m_moduleManager->DestroyMainModule()) {
			return false;
		}

		for (auto obj : m_commonObject->GetObjects()) {

			if (obj->IsDeleted())
				continue;

			if (!obj->OnAfterCloseMetaObject())
				return false;

			if (!CloseChildMetadata(obj, (flags & forceCloseFlag) != 0, false))
				return false;
		}

		if (!m_commonObject->OnAfterCloseMetaObject()) {
			wxASSERT_MSG(false, "m_commonObject->OnAfterCloseMetaObject() == false");
			return false;
		}
	}

	m_configOpened = false;

	return true;
}

bool CConfigFileMetadata::CloseChildMetadata(IMetaObject* metaParent, int flags, bool before)
{
	for (auto obj : metaParent->GetObjects()) {

		if (obj->IsDeleted())
			continue;

		if (before && !obj->OnBeforeCloseMetaObject())
			return false;

		if (!before && !obj->OnAfterCloseMetaObject())
			return false;

		if (!CloseChildMetadata(obj, flags, before))
			return false;
	}

	return true;
}

bool CConfigFileMetadata::ClearMetadata()
{
	for (auto obj : m_commonObject->GetObjects()) {

		if (!obj->OnDeleteMetaObject())
			return false;

		if (!ClearChildMetadata(obj))
			return false;

		m_commonObject->RemoveChild(obj);
	}

	if (!m_commonObject->OnDeleteMetaObject()) {
		wxASSERT_MSG(false, "m_commonObject->OnDeleteMetaObject() == false");
		return false;
	}

	return true;
}

bool CConfigFileMetadata::ClearChildMetadata(IMetaObject* metaParent)
{
	for (auto obj : metaParent->GetObjects()) {

		if (!obj->OnDeleteMetaObject())
			return false;

		if (!ClearChildMetadata(obj))
			return false;

		metaParent->RemoveChild(obj);
	}

	metaParent->DecrRef();
	return true;
}

#include <fstream>

bool CConfigFileMetadata::LoadFromFile(const wxString& fileName)
{
	//close data 
	if (CConfigFileMetadata::ConfigOpened()) {
		if (!CloseMetadata(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseMetadata() == false");
			return false;

		}
	}

	//clear data 
	if (!ClearMetadata()) {
		wxASSERT_MSG(false, "ClearMetadata() == false");
		return false;
	}

	std::ifstream in(fileName.ToStdWstring(), std::ios::in | std::ios::binary);

	if (!in.is_open())
		return false;

	//go to end
	in.seekg(0, in.end);
	//get size of file
	std::streamsize fsize = in.tellg();
	//go to beginning
	in.seekg(0, in.beg);

	wxMemoryBuffer tempBuffer(fsize);
	in.read((char*)tempBuffer.GetWriteBuf(fsize), fsize);

	CMemoryReader readerData(tempBuffer.GetData(), tempBuffer.GetBufSize());

	if (readerData.eof())
		return false;

	in.close();

	//Save header info 
	if (!LoadHeader(readerData))
		return false;

	//loading common metadata and child item
	if (!LoadCommonMetadata(g_metaCommonMetadataCLSID, readerData)) {
		//clear data 
		if (!ClearMetadata()) {
			wxASSERT_MSG(false, "ClearMetadata() == false");
		}
		return false;
	}

	return RunMetadata(onlyLoadFlag);
}

bool CConfigFileMetadata::LoadHeader(CMemoryReader& readerData)
{
	CMemoryReader* readerMemory = readerData.open_chunk(eHeaderBlock);

	if (!readerMemory)
		return false;

	u64 metaSign = readerMemory->r_u64();

	if (metaSign != sign_metadata)
		return false;

	wxString metaGuid;
	readerMemory->r_stringZ(metaGuid);

	readerMemory->close();
	return true;
}

bool CConfigFileMetadata::LoadCommonMetadata(const CLASS_ID& clsid, CMemoryReader& readerData)
{
	CMemoryReader* readerMemory = readerData.open_chunk(clsid);

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id);

	if (!readerMetaMemory)
		return true;

	std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));

	m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (!m_commonObject->LoadMetaObject(this, *readerDataMemory))
		return false;

	std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));

	if (readerChildMemory) {
		if (!LoadMetadata(clsid, *readerChildMemory, m_commonObject))
			return false;
	}

	return true;
}

bool CConfigFileMetadata::LoadMetadata(const CLASS_ID&, CMemoryReader& readerData, IMetaObject* metaParent)
{
	CLASS_ID clsid = 0;
	CMemoryReader* prevReaderMemory = NULL;

	while (!readerData.eof())
	{
		CMemoryReader* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);

		if (!readerMemory)
			break;

		u64 meta_id = 0;
		CMemoryReader* prevReaderMetaMemory = NULL;

		while (!readerMemory->eof())
		{
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);
			wxString classType = CValue::GetNameObjectFromID(clsid);
			wxASSERT(classType.Length() > 0);
			IMetaObject* newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(classType);
			wxASSERT(newMetaObject);

			newMetaObject->SetClsid(clsid);
			if (metaParent) {
				newMetaObject->SetParent(metaParent);
				metaParent->AddChild(newMetaObject);
			}
			newMetaObject->SetReadOnly(!m_metaReadOnly);
			if (metaParent) {
				metaParent->AppendChild(newMetaObject);
			}
			newMetaObject->IncrRef();

			std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory) {
				if (!LoadChildMetadata(clsid, *readerChildMemory, newMetaObject))
					return false;
			}

			std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));

			if (!newMetaObject->LoadMetaObject(this, *readerDataMemory))
				return false;

			prevReaderMetaMemory = readerMetaMemory;
		}

		prevReaderMemory = readerMemory;
	};

	return true;
}

bool CConfigFileMetadata::LoadChildMetadata(const CLASS_ID&, CMemoryReader& readerData, IMetaObject* metaParent)
{
	CLASS_ID clsid = 0;
	CMemoryReader* prevReaderMemory = NULL;

	while (!readerData.eof())
	{
		CMemoryReader* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);

		if (!readerMemory)
			break;

		u64 meta_id = 0;
		CMemoryReader* prevReaderMetaMemory = NULL;

		while (!readerMemory->eof())
		{
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);
			wxString classType = CValue::GetNameObjectFromID(clsid);
			wxASSERT(classType.Length() > 0);
			IMetaObject* newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(classType);
			wxASSERT(newMetaObject);

			newMetaObject->SetClsid(clsid);
			newMetaObject->SetReadOnly(!m_metaReadOnly);

			if (metaParent) {
				newMetaObject->SetParent(metaParent);
				metaParent->AddChild(newMetaObject);
			}
			if (metaParent) {
				metaParent->AppendChild(newMetaObject);
			}

			newMetaObject->IncrRef();

			std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory) {
				if (!LoadChildMetadata(clsid, *readerChildMemory, newMetaObject))
					return false;
			}

			std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
			if (!newMetaObject->LoadMetaObject(this, *readerDataMemory))
				return false;

			prevReaderMetaMemory = readerMetaMemory;
		}

		prevReaderMemory = readerMemory;
	}

	return true;
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

CConfigMetadata::CConfigMetadata(bool readOnly) : CConfigFileMetadata(readOnly)
{
	m_sConfigPath = wxT("sys.database");
	m_sDefaultSource = GetConfigTableName();
}

#include <wx/base64.h>
#include "email/utils/wxmd5.hpp"

bool CConfigMetadata::LoadMetadata(int flags)
{
	if (!databaseLayer->IsOpen()) {
		return false;
	}

	// load config params
	DatabaseResultSet* resultConfigParams =
		databaseLayer->RunQueryWithResults("SELECT guid, name FROM %s; ", GetConfigParamsTableName());

	if (!resultConfigParams)
		return false;

	//load metadata from DB 
	while (resultConfigParams->Next()) {
		if (resultConfigParams->GetResultString(wxT("name")) == wxT("configuration_id")) {
			m_metaGuid = resultConfigParams->GetResultString(wxT("guid"));
		}
	}

	resultConfigParams->Close();

	if ((flags & onlyLoadFlag) == 0) {
		//Initialize debugger
		if (!appData->DesignerMode()) {
			debugServerInit();
		}
		else {
			debugClientInit();
		}
	}

	// load config
	DatabaseResultSet* resultConfig =
		databaseLayer->RunQueryWithResults("SELECT binaryData, dataSize FROM %s; ", m_sDefaultSource);

	if (!resultConfig)
		return false;

	//load metadata from DB 
	if (resultConfig->Next()) {

		//close if opened
		if (CConfigMetadata::ConfigOpened()) {
			if (!CloseMetadata(forceCloseFlag)) {
				return false;
			}
		}

		//clear data 
		if (!ClearMetadata()) {
			wxASSERT_MSG(false, "ClearMetadata() == false");
			return false;
		}

		wxMemoryBuffer binaryData;
		resultConfig->GetResultBlob(wxT("binaryData"), binaryData);
		CMemoryReader metaReader(binaryData.GetData(), resultConfig->GetResultInt(wxT("dataSize")));

		//check is file empty
		if (metaReader.eof())
			return false;

		//check metadata 
		if (!LoadHeader(metaReader))
			return false;

		//load metadata 
		if (!LoadCommonMetadata(g_metaCommonMetadataCLSID, metaReader))
			return false;

		m_md5Hash = wxMD5::ComputeMd5(
			wxBase64Encode(binaryData.GetData(), binaryData.GetDataLen())
		);
	}

	resultConfig->Close();

	return RunMetadata(flags);
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

CConfigStorageMetadata::CConfigStorageMetadata(bool readOnly) : CConfigMetadata(readOnly),
m_metaConfig(new CConfigMetadata(readOnly)), m_configSave(true)
{
	m_sDefaultSource = GetConfigSaveTableName();
}

CConfigStorageMetadata::~CConfigStorageMetadata() {
	wxDELETE(m_metaConfig);
}

#include "metaObjects/objects/constant.h"

bool CConfigStorageMetadata::CreateMetadata()
{
	if (!databaseLayer->IsOpen())
		return false;

	//new database
	if (!databaseLayer->TableExists(GetConfigParamsTableName())) {
		m_metaGuid = wxNewGuid;
	}

	//db for designer 
	if (!databaseLayer->TableExists(GetConfigSaveTableName())) {
		databaseLayer->RunQuery("CREATE TABLE %s ("
			"fileName VARCHAR(128) NOT NULL PRIMARY KEY,"
			"attributes INTEGER,"
			"dataSize INTEGER NOT NULL," 			//binary medatadata
			"binaryData BLOB NOT NULL);", IConfigMetadata::GetConfigSaveTableName());         	//size of binary medatadata

		databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s ("
			"fileName);", IConfigMetadata::GetConfigSaveTableName(), IConfigMetadata::GetConfigSaveTableName());
	}

	//db for enterprise - TODO
	if (!databaseLayer->TableExists(GetConfigTableName())) {
		databaseLayer->RunQuery("CREATE TABLE %s ("
			"fileName VARCHAR(128) NOT NULL PRIMARY KEY,"
			"attributes INTEGER,"
			"dataSize INTEGER NOT NULL," 			//binary medatadata
			"binaryData BLOB NOT NULL);", GetConfigTableName());         	//size of binary medatadata

		databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s ("
			"fileName);", GetConfigTableName(), GetConfigTableName());
	}

	//compile data for better performance - TODO
	if (!databaseLayer->TableExists(GetCompileDataTableName())) {
		databaseLayer->RunQuery("CREATE TABLE %s ("
			"fileName VARCHAR(128) NOT NULL,"
			"dataSize INTEGER NOT NULL," 			//binary medatadata
			"binaryData BLOB NOT NULL);", GetCompileDataTableName());         	//size of binary medatadata

		databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s ("
			"fileName);", GetCompileDataTableName(), GetCompileDataTableName());
	}

	//create users or nothin 
	if (!databaseLayer->TableExists(GetUsersTableName())) {
		databaseLayer->RunQuery("CREATE TABLE %s ("
			"guid              VARCHAR(36)   NOT NULL PRIMARY KEY,"
			"name              VARCHAR(64)  NOT NULL,"
			"fullName          VARCHAR(128)  NOT NULL,"
			"changed		   TIMESTAMP  NOT NULL,"
			"dataSize          INTEGER       NOT NULL,"
			"binaryData        BLOB      NOT NULL);", GetUsersTableName());

		databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s ("
			"guid,"
			"name);", GetUsersTableName(), GetUsersTableName());
	}

	// active users
	if (!databaseLayer->TableExists(GetActiveUsersTableName())) {
		databaseLayer->RunQuery("CREATE TABLE %s ("
			"session              VARCHAR(36)   NOT NULL PRIMARY KEY,"
			"userName             VARCHAR(64)  NOT NULL,"
			"application	   INTEGER  NOT NULL,"
			"started		   TIMESTAMP  NOT NULL,"
			"lastActive		   TIMESTAMP  NOT NULL,"
			"computer          VARCHAR(128) NOT NULL);", GetActiveUsersTableName());

		databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s ("
			"session,"
			"userName);", GetActiveUsersTableName(), GetActiveUsersTableName());
	}

	//config params 
	if (!databaseLayer->TableExists(GetConfigParamsTableName())) {
		int retCode = databaseLayer->RunQuery("CREATE TABLE %s ("
			"flags			   INTEGER       DEFAULT 0 NOT NULL,"
			"guid              VARCHAR(36)   NOT NULL PRIMARY KEY,"
			"name              VARCHAR(64)  NOT NULL,"
			"dataSize          INTEGER    DEFAULT 0 NOT NULL,"
			"binaryData        BLOB);", GetConfigParamsTableName());

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
		retCode = databaseLayer->RunQuery("INSERT INTO %s (guid, name) VALUES ('%s', '%s');", GetConfigParamsTableName(), m_metaGuid.str(), wxT("configuration_id"));

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
	}

	//create constat 
	wxString constName =
		CMetaConstantObject::GetTableNameDB();
	if (!databaseLayer->TableExists(constName)) {
		int retCode = databaseLayer->RunQuery("CREATE TABLE %s (RECORD_KEY CHAR DEFAULT '6');", constName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
		retCode = databaseLayer->RunQuery("INSERT INTO %s (RECORD_KEY) VALUES ('6');", constName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
	}

	return databaseLayer->IsOpen();
}

bool CConfigStorageMetadata::SaveMetadata(int flags)
{
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
	//begin transaction 
	databaseLayer->BeginTransaction();
#endif 

	//remove old tables (if need)
	if ((flags & saveConfigFlag) != 0) {

		//Delete common object
		if (!DeleteCommonMetadata(g_metaCommonMetadataCLSID)) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
			databaseLayer->RollBack(); return false;
#else 
			return false;
#endif
		}

		IMetaObject* commonObject = m_metaConfig->GetCommonMetaObject();
		wxASSERT(commonObject);

		for (auto obj : commonObject->GetObjects()) {
			IMetaObject* foundedMeta =
				m_commonObject->FindByName(obj->GetDocPath());
			if (foundedMeta == NULL) {
				bool ret = obj->DeleteMetaTable(this);
				if (!ret) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
					databaseLayer->RollBack(); return false;
#else 
					return false;
#endif
				}
			}
		}

		for (auto obj : m_commonObject->GetObjects()) {
			IMetaObject* foundedMeta =
				commonObject->FindByName(obj->GetDocPath());
			wxASSERT(obj);
			bool ret = true;
			if (foundedMeta == NULL) {
				ret = obj->CreateMetaTable(m_metaConfig);
			}
			else {
				ret = obj->UpdateMetaTable(m_metaConfig, foundedMeta);
			}

			if (!ret) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
				databaseLayer->RollBack(); return false;
#else 
				return false;
#endif
			}
		}
	}

	//common data
	CMemoryWriter writterData;

	//Save header info 
	if (!SaveHeader(writterData)) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
		databaseLayer->RollBack(); return false;
#else 
		return false;
#endif
	}

	//Save common object
	if (!SaveCommonMetadata(g_metaCommonMetadataCLSID, writterData, flags)) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
		databaseLayer->RollBack(); return false;
#else 
		return false;
#endif
	}

	PreparedStatement* prepStatement =
		databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (fileName, dataSize, binaryData) VALUES(?, ?, ?) MATCHING (fileName); ", GetConfigSaveTableName());

	if (!prepStatement)
		return false;

	prepStatement->SetParamString(1, m_sConfigPath);
	prepStatement->SetParamInt(2, writterData.size());
	prepStatement->SetParamBlob(3, writterData.pointer(), writterData.size());

	if (prepStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
		databaseLayer->RollBack();
#else 
		return false;
#endif
	}

	m_md5Hash = wxMD5::ComputeMd5(
		wxBase64Encode(writterData.pointer(), writterData.size())
	);

	m_configSave = (flags & saveConfigFlag) != 0;

	if (!databaseLayer->CloseStatement(prepStatement)) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
		databaseLayer->RollBack();
#else 
		return false;
#endif
	}

	Modify(false);

	if ((flags & saveConfigFlag) != 0) {

		bool hasError =
			databaseLayer->RunQuery("DELETE FROM %s;", GetConfigTableName()) == DATABASE_LAYER_QUERY_RESULT_ERROR;
		hasError = hasError ||
			databaseLayer->RunQuery("INSERT INTO %s SELECT * FROM %s;", GetConfigTableName(), GetConfigSaveTableName()) == DATABASE_LAYER_QUERY_RESULT_ERROR;

		if (hasError)
			return false;

		if (!m_metaConfig->LoadMetadata(onlyLoadFlag)) {
#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
			databaseLayer->RollBack();
#else 
			return false;
#endif
		}
	}
	else {
		Modify(true);
	}

#if defined(_USE_SAVE_METADATA_IN_TRANSACTION)
	databaseLayer->Commit();
#endif 

	return true;
}

bool CConfigStorageMetadata::RoolbackToConfigDatabase()
{
	bool hasError =
		databaseLayer->RunQuery("DELETE FROM %s;", GetConfigSaveTableName()) == DATABASE_LAYER_QUERY_RESULT_ERROR;
	hasError = hasError ||
		databaseLayer->RunQuery("INSERT INTO %s SELECT * FROM %s;", GetConfigSaveTableName(), GetConfigTableName()) == DATABASE_LAYER_QUERY_RESULT_ERROR;

	if (hasError)
		return false;

	//close data 
	if (CConfigMetadata::ConfigOpened()) {
		if (!CloseMetadata(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseMetadata() == false");
			return false;
		}
	}

	//clear data 
	if (!ClearMetadata()) {
		wxASSERT_MSG(false, "ClearMetadata() == false");
		return false;
	}

	return LoadMetadata();
}

bool CConfigStorageMetadata::SaveToFile(const wxString& fileName)
{
	//common data
	CMemoryWriter writterData;

	//Save header info 
	if (!SaveHeader(writterData))
		return false;

	//Save common object
	if (!SaveCommonMetadata(g_metaCommonMetadataCLSID, writterData, saveToFileFlag))
		return false;

	std::ofstream datafile;
	datafile.open(fileName.ToStdWstring(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (writterData.pointer()), writterData.size());
	datafile.close();

	return true;
}

bool CConfigStorageMetadata::SaveHeader(CMemoryWriter& writterData)
{
	CMemoryWriter writterMemory;
	writterMemory.w_u64(sign_metadata); //sign 
	writterMemory.w_stringZ(m_commonObject->GetDocPath()); //guid conf 

	writterData.w_chunk(eHeaderBlock, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CConfigStorageMetadata::SaveCommonMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, int flags)
{
	//Save common object
	CMemoryWriter writterMemory;

	CMemoryWriter writterMetaMemory;
	CMemoryWriter writterDataMemory;

	if (!m_commonObject->SaveMetaObject(this, writterDataMemory, flags)) {
		return false;
	}

	writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());

	CMemoryWriter writterChildMemory;

	if (!SaveMetadata(clsid, writterChildMemory, flags))
		return false;

	writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
	writterMemory.w_chunk(m_commonObject->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());

	writterData.w_chunk(clsid, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CConfigStorageMetadata::SaveMetadata(const CLASS_ID&, CMemoryWriter& writterData, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	for (auto obj : m_commonObject->GetObjects()) {
		if (saveToFile && obj->IsDeleted())
			continue;
		CMemoryWriter writterMemory;
		CMemoryWriter writterMetaMemory;
		CMemoryWriter writterDataMemory;
		if (!obj->SaveMetaObject(this, writterDataMemory, flags)) {
			return false;
		}
		writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
		CMemoryWriter writterChildMemory;
		if (!SaveChildMetadata(obj->GetClsid(), writterChildMemory, obj, flags)) {
			return false;
		}
		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(obj->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());
		writterData.w_chunk(obj->GetClsid(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

bool CConfigStorageMetadata::SaveChildMetadata(const CLASS_ID&, CMemoryWriter& writterData, IMetaObject* metaParent, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	for (auto obj : metaParent->GetObjects()) {
		if (saveToFile && obj->IsDeleted())
			continue;
		CMemoryWriter writterMemory;
		CMemoryWriter writterMetaMemory;
		CMemoryWriter writterDataMemory;
		if (!obj->SaveMetaObject(this, writterDataMemory, flags)) {
			return false;
		}
		writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
		CMemoryWriter writterChildMemory;
		if (!SaveChildMetadata(obj->GetClsid(), writterChildMemory, obj, flags)) {
			return false;
		}
		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(obj->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());
		writterData.w_chunk(obj->GetClsid(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

bool CConfigStorageMetadata::DeleteCommonMetadata(const CLASS_ID& clsid)
{
	return DeleteMetadata(clsid);
}

bool CConfigStorageMetadata::DeleteMetadata(const CLASS_ID& clsid)
{
	for (auto obj : m_commonObject->GetObjects()) {
		if (obj->IsDeleted()) {
			if (!obj->DeleteMetaObject(this)) {
				return false;
			}
		}
		if (!DeleteChildMetadata(obj->GetClsid(), obj)) {
			return false;
		}
		if (obj->IsDeleted()) {
			m_commonObject->RemoveChild(obj);
			obj->DecrRef();
		}
	}

	return true;
}

bool CConfigStorageMetadata::DeleteChildMetadata(const CLASS_ID& clsid, IMetaObject* metaParent)
{
	for (auto obj : metaParent->GetObjects()) {
		if (obj->IsDeleted()) {
			if (!obj->DeleteMetaObject(this)) {
				return false;
			}
		}
		if (!DeleteChildMetadata(obj->GetClsid(), obj)) {
			return false;
		}
		if (obj->IsDeleted()) {
			metaParent->RemoveChild(obj);
			obj->DecrRef();
		}
	}

	return true;
}