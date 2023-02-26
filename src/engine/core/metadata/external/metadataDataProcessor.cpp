////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : external metadata - for dataProcessors, reports
////////////////////////////////////////////////////////////////////////////

#include "metadataDataProcessor.h"
#include "appData.h"

CMetadataDataProcessor::CMetadataDataProcessor() : IMetadata(),
m_commonObject(NULL),
m_ownerMeta(NULL),
m_version(version_oes_last)
{
	//create main metaObject
	m_commonObject = new CMetaObjectDataProcessorExternal;
	m_commonObject->SetClsid(g_metaExternalDataProcessorCLSID);
	m_commonObject->SetName(
		IMetadata::GetNewName(g_metaExternalDataProcessorCLSID, NULL, m_commonObject->GetClassName())
	);

	if (m_commonObject->OnCreateMetaObject(this)) {
		m_moduleManager = new CExternalDataProcessorModuleManager(this, m_commonObject);
		m_moduleManager->IncrRef();
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
	}

	m_commonObject->IncrRef();
	wxASSERT(m_moduleManager);

	m_ownerMeta = this;
}

CMetadataDataProcessor::CMetadataDataProcessor(IMetadata* metaData, CMetaObjectDataProcessor* srcDataProcessor) : IMetadata(),
m_commonObject(srcDataProcessor),
m_ownerMeta(NULL),
m_version(version_oes_last)
{
	if (srcDataProcessor == NULL) {
		IMetaObject* commonMetaObject = metaData->GetCommonMetaObject();
		wxASSERT(commonMetaObject);
		//create main metaObject
		m_commonObject = new CMetaObjectDataProcessor();
		m_commonObject->SetClsid(g_metaDataProcessorCLSID);
		m_commonObject->SetName(
			IMetadata::GetNewName(g_metaDataProcessorCLSID, NULL, m_commonObject->GetClassName())
		);
		if (commonMetaObject != NULL) {
			m_commonObject->SetParent(commonMetaObject);
			commonMetaObject->AddChild(m_commonObject);
		}
		if (commonMetaObject != NULL) {
			commonMetaObject->AppendChild(m_commonObject);
		}
	}

	m_commonObject->IncrRef();
	m_ownerMeta = metaData;
}

CMetadataDataProcessor::~CMetadataDataProcessor()
{
	if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		if (!m_moduleManager->DestroyMainModule()) {
			wxASSERT_MSG(false, "m_moduleManager->DestroyMainModule() == false");
		}
		//delete module manager
		if (m_moduleManager != NULL) {
			m_moduleManager->DecrRef();
		}
		//clear data 
		if (!ClearMetadata()) {
			wxASSERT_MSG(false, "ClearMetadata() == false");
		}
	}
	if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		//delete common metaObject
		m_commonObject->DecrRef();
	}
}

#include "core/metadata/singleClass.h"
#include "utils/stringUtils.h"

CValue* CMetadataDataProcessor::CreateObjectRef(const CLASS_ID& clsid, CValue** paParams, const long lSizeArray)
{
	auto itFounded = std::find_if(s_factoryMetaObjects.begin(), s_factoryMetaObjects.end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		}
	);

	if (itFounded != s_factoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		CValue* newObject = singleObject->CreateObject();
		wxASSERT(newObject);
		//newObject->PrepareNames();
		if (lSizeArray > 0) {
			if (!newObject->Init(paParams, lSizeArray)) {
				if (!appData->DesignerMode()) {
					wxDELETE(newObject);
					CTranslateError::Error("Error initializing object '%ul'", clsid);
				}
			}
		}
		else {
			if (!newObject->Init()) {
				if (!appData->DesignerMode()) {
					wxDELETE(newObject);
					CTranslateError::Error("Error initializing object '%ul'", clsid);
				}
			}
		}
		return newObject;
	}
	return metadata->CreateObjectRef(clsid, paParams, lSizeArray);
}

bool CMetadataDataProcessor::IsRegisterObject(const wxString& className) const
{
	if (!IMetadata::IsRegisterObject(className))
		return metadata->IsRegisterObject(className);
	return true;
}

bool CMetadataDataProcessor::IsRegisterObject(const wxString& className, eObjectType objectType) const
{
	if (!IMetadata::IsRegisterObject(className, objectType))
		return metadata->IsRegisterObject(className);
	return true;
}

bool CMetadataDataProcessor::IsRegisterObject(const wxString& className, eObjectType objectType, eMetaObjectType refType) const
{
	if (!IMetadata::IsRegisterObject(className, objectType, refType))
		return metadata->IsRegisterObject(className, objectType, refType);
	return true;
}

bool CMetadataDataProcessor::IsRegisterObject(const CLASS_ID& clsid) const
{
	if (!IMetadata::IsRegisterObject(clsid))
		return metadata->IsRegisterObject(clsid);
	return true;
}

CLASS_ID CMetadataDataProcessor::GetIDObjectFromString(const wxString& clsName) const
{
	auto itFounded = std::find_if(s_factoryMetaObjects.begin(), s_factoryMetaObjects.end(), [clsName](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(clsName, singleObject->GetClassName());
		});

	if (itFounded != s_factoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		return singleObject->GetTypeClass();
	}

	return metadata->GetIDObjectFromString(clsName);
}

wxString CMetadataDataProcessor::GetNameObjectFromID(const CLASS_ID& clsid, bool upper) const
{
	auto itFounded = std::find_if(s_factoryMetaObjects.begin(), s_factoryMetaObjects.end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		});

	if (itFounded != s_factoryMetaObjects.end()) {
		IObjectValueAbstract* singleObject = *itFounded;
		wxASSERT(singleObject);
		return upper ? singleObject->GetClassName().Upper() : singleObject->GetClassName();
	}

	return metadata->GetNameObjectFromID(clsid, upper);
}

IMetaTypeObjectValueSingle* CMetadataDataProcessor::GetTypeObject(const CLASS_ID& clsid) const
{
	auto itFounded = std::find_if(s_factoryMetaObjects.begin(), s_factoryMetaObjects.end(), [clsid](IMetaTypeObjectValueSingle* singleObject) {
		return clsid == singleObject->GetTypeClass(); }
	);

	if (itFounded != s_factoryMetaObjects.end()) {
		return *itFounded;
	}

	return metadata->GetTypeObject(clsid);
}

IMetaTypeObjectValueSingle* CMetadataDataProcessor::GetTypeObject(const IMetaObject* metaValue, eMetaObjectType refType) const
{
	auto itFounded = std::find_if(s_factoryMetaObjects.begin(), s_factoryMetaObjects.end(), [metaValue, refType](IMetaTypeObjectValueSingle* singleObject) {
		return refType == singleObject->GetMetaType() &&
		metaValue == singleObject->GetMetaObject();
		});

	if (itFounded != s_factoryMetaObjects.end()) {
		return *itFounded;
	}

	return metadata->GetTypeObject(metaValue, refType);
}

wxArrayString CMetadataDataProcessor::GetAvailableObjects(eMetaObjectType refType) const
{
	return metadata->GetAvailableObjects(refType);
}

IObjectValueAbstract* CMetadataDataProcessor::GetAvailableObject(const CLASS_ID& clsid) const
{
	return metadata->GetAvailableObject(clsid);
}

IObjectValueAbstract* CMetadataDataProcessor::GetAvailableObject(const wxString& className) const
{
	return metadata->GetAvailableObject(className);
}

std::vector<IMetaTypeObjectValueSingle*> CMetadataDataProcessor::GetAvailableSingleObjects() const
{
	return metadata->GetAvailableSingleObjects();
}

std::vector<IMetaTypeObjectValueSingle*> CMetadataDataProcessor::GetAvailableSingleObjects(const CLASS_ID& clsid, eMetaObjectType refType) const
{
	return metadata->GetAvailableSingleObjects(clsid, refType);
}

std::vector<IMetaTypeObjectValueSingle*> CMetadataDataProcessor::GetAvailableSingleObjects(eMetaObjectType refType) const
{
	return metadata->GetAvailableSingleObjects(refType);
}

IMetaObject* CMetadataDataProcessor::GetMetaObject(const meta_identifier_t& id)
{
	IMetaObject* metaObject = IMetadata::GetMetaObject(id);

	if (metaObject != NULL) {
		return metaObject;
	}

	return metadata->GetMetaObject(id);
}

bool CMetadataDataProcessor::CreateMetadata()
{
	return true;
}

bool CMetadataDataProcessor::LoadMetadata()
{
	return RunMetadata();
}

bool CMetadataDataProcessor::SaveMetadata()
{
	return true;
}

bool CMetadataDataProcessor::ClearMetadata()
{
	if (!ClearChildMetadata(m_commonObject))
		return false;
	return true;
}

bool CMetadataDataProcessor::ClearChildMetadata(IMetaObject* metaParent)
{
	for (auto obj : metaParent->GetObjects()) {
		if (!obj->IsDeleted() && !obj->OnDeleteMetaObject())
			return false;
		if (!ClearChildMetadata(obj))
			return false;
	}

	if (metaParent != m_commonObject) {
		metaParent->DecrRef();
	}

	return true;
}

bool CMetadataDataProcessor::RunMetadata(int flags)
{
	if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {

		if (!m_commonObject->OnBeforeRunMetaObject(flags)) {
			wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
			return false;
		}
		if (!RunChildMetadata(m_commonObject, flags, true)) {
			return false;
		}

		if (m_moduleManager->CreateMainModule()) {
			if (!m_commonObject->OnAfterRunMetaObject(flags)) {
				wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
				return false;
			}
			if (!RunChildMetadata(m_commonObject, flags, false)) {
				return false;
			}
			if (!m_moduleManager->StartMainModule()) {
				return false;
			}
			return true;
		}
	}
	else if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {

		if (!m_commonObject->OnBeforeRunMetaObject(flags)) {
			wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
			return false;
		}
		if (!RunChildMetadata(m_commonObject, flags, true)) {
			return false;
		}
		if (!m_commonObject->OnAfterRunMetaObject(flags)) {
			wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
			return false;
		}
		if (!RunChildMetadata(m_commonObject, flags, false)) {
			return false;
		}

		return true;
	}

	return false;
}

bool CMetadataDataProcessor::RunChildMetadata(IMetaObject* metaParent, int flags, bool before)
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

bool CMetadataDataProcessor::CloseMetadata(int flags)
{
	if (!m_commonObject->IsDeleted()) {
		if (!CloseChildMetadata(m_commonObject, (flags & forceCloseFlag) != 0, true)) {
			return false;
		}
	}

	if (m_moduleManager &&
		m_moduleManager->ExitMainModule((flags & forceCloseFlag) != 0)) {
		return CloseChildMetadata(m_commonObject, (flags & forceCloseFlag) != 0, false);
	}

	return false;
}

bool CMetadataDataProcessor::CloseChildMetadata(IMetaObject* metaParent, int flags, bool before)
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

#include <fstream>

bool CMetadataDataProcessor::LoadFromFile(const wxString& fileName)
{
	if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
		if (!m_commonObject->OnCreateMetaObject(m_ownerMeta))
			return false;
	}
	else if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		//close data 
		if (!CloseMetadata(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseMetadata() == false");
			return false;
		}
		//clear data 
		if (!ClearMetadata()) {
			wxASSERT_MSG(false, "ClearMetadata() == false");
			return false;
		}
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

	m_fullPath = fileName;

	//loading common metadata and child item
	if (!LoadCommonMetadata(g_metaExternalDataProcessorCLSID, readerData)) {
		if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
			//clear data 
			if (!ClearMetadata()) {
				wxASSERT_MSG(false, "ClearMetadata() == false");
			}
		}
		return false;
	}

	if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
		m_commonObject->BuildNewName();
	}

	return LoadMetadata();
}

bool CMetadataDataProcessor::SaveToFile(const wxString& fileName)
{
	//common data
	CMemoryWriter writterData;

	//Save header info 
	if (!SaveHeader(writterData))
		return false;

	m_fullPath = fileName;

	//Save common object
	if (!SaveCommonMetadata(g_metaExternalDataProcessorCLSID, writterData))
		return false;

	//Delete common object
	if (!DeleteCommonMetadata(g_metaExternalDataProcessorCLSID))
		return false;

	std::ofstream datafile;
	datafile.open(fileName.ToStdWstring(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (writterData.pointer()), writterData.size());
	datafile.close();

	return true;
}

bool CMetadataDataProcessor::LoadHeader(CMemoryReader& readerData)
{
	CMemoryReader* readerMemory = readerData.open_chunk(eHeaderBlock);

	if (!readerMemory)
		return false;

	u64 metaSign = readerMemory->r_u64();

	if (metaSign != sign_dataProcessor)
		return false;

	m_version = readerMemory->r_u32();

	wxString metaGuid;
	readerMemory->r_stringZ(metaGuid);

	readerMemory->close();
	return true;
}

bool CMetadataDataProcessor::LoadCommonMetadata(const CLASS_ID& clsid, CMemoryReader& readerData)
{
	CMemoryReader* readerMemory = readerData.open_chunk(clsid);

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id);

	if (!readerMetaMemory)
		return true;

	std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
	if (readerChildMemory) {
		if (!LoadChildMetadata(clsid, *readerChildMemory, m_commonObject))
			return false;
	}

	std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
	m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (!m_commonObject->LoadMetaObject(m_ownerMeta, *readerDataMemory))
		return false;

	if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
		m_commonObject->ResetId();
	}

	return true;
}

bool CMetadataDataProcessor::LoadChildMetadata(const CLASS_ID&, CMemoryReader& readerData, IMetaObject* metaParent)
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

		while (!readerData.eof())
		{
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);
			IMetaObject* newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(clsid);
			wxASSERT(newMetaObject);

			newMetaObject->SetClsid(clsid);
			newMetaObject->SetReadOnly(!m_metaReadOnly);

			if (metaParent) {
				newMetaObject->SetParent(metaParent);
				metaParent->AddChild(newMetaObject);
			}
			newMetaObject->IncrRef();

			std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory != NULL) {
				if (!LoadChildMetadata(clsid, *readerChildMemory, newMetaObject))
					return false;
			}

			std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
			if (!newMetaObject->LoadMetaObject(m_ownerMeta, *readerDataMemory))
				return false;
			if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
				newMetaObject->ResetId();
			}
			if (metaParent != NULL) {
				metaParent->AppendChild(newMetaObject);
			}

			prevReaderMetaMemory = readerMetaMemory;
		}

		prevReaderMemory = readerMemory;
	}

	return true;
}

bool CMetadataDataProcessor::SaveHeader(CMemoryWriter& writterData)
{
	CMemoryWriter writterMemory;
	writterMemory.w_u64(sign_dataProcessor); //sign 
	writterMemory.w_u32(m_version); // version 1 - DEFAULT
	writterMemory.w_stringZ(m_commonObject->GetDocPath()); //guid conf 

	writterData.w_chunk(eHeaderBlock, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CMetadataDataProcessor::SaveCommonMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, bool saveToFile)
{
	//Save common object
	CMemoryWriter writterMemory;

	CMemoryWriter writterMetaMemory;
	CMemoryWriter writterDataMemory;

	if (!m_commonObject->SaveMetaObject(m_ownerMeta, writterDataMemory, saveToFile)) {
		return false;
	}

	writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());

	CMemoryWriter writterChildMemory;

	if (!SaveChildMetadata(clsid, writterChildMemory, m_commonObject, saveToFile))
		return false;

	writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
	writterMemory.w_chunk(m_commonObject->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());

	writterData.w_chunk(clsid, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CMetadataDataProcessor::SaveChildMetadata(const CLASS_ID&, CMemoryWriter& writterData, IMetaObject* metaParent, bool saveToFile)
{
	for (auto obj : metaParent->GetObjects())
	{
		CMemoryWriter writterMemory;

		if (obj->IsDeleted())
			continue;

		CMemoryWriter writterMetaMemory;
		CMemoryWriter writterDataMemory;

		if (!obj->SaveMetaObject(m_ownerMeta, writterDataMemory, saveToFile)) {
			return false;
		}

		writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());

		CMemoryWriter writterChildMemory;

		if (!SaveChildMetadata(obj->GetClsid(), writterChildMemory, obj, saveToFile)) {
			return false;
		}

		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(obj->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());

		writterData.w_chunk(obj->GetClsid(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

bool CMetadataDataProcessor::DeleteCommonMetadata(const CLASS_ID& clsid)
{
	return DeleteChildMetadata(clsid, m_commonObject);
}

bool CMetadataDataProcessor::DeleteChildMetadata(const CLASS_ID& clsid, IMetaObject* metaParent)
{
	for (auto obj : metaParent->GetObjects())
	{
		if (obj->IsDeleted()) {

			if (!obj->DeleteMetaObject(m_ownerMeta))
				return false;

			if (!DeleteChildMetadata(obj->GetClsid(), obj))
				return false;

			metaParent->RemoveChild(obj);

			obj->DecrRef();
		}
		else {
			if (!DeleteChildMetadata(obj->GetClsid(), obj))
				return false;
		}
	}

	return true;
}