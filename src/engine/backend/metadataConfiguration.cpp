#include "metadataConfiguration.h"

#include "backend/debugger/debugClient.h"
#include "backend/debugger/debugServer.h"

#include "backend/backend_mainFrame.h"
#include "backend/appData.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////
IMetaDataConfiguration* IMetaDataConfiguration::s_instance = nullptr;
//////////////////////////////////////////////////////////////////////////////////////////////////////

bool IMetaDataConfiguration::Initialize(eRunMode mode, const int flags)
{
	if (s_instance == nullptr) {

		switch (mode)
		{
		case eLAUNCHER_MODE: break;
		case eDESIGNER_MODE:
			s_instance = new CMetaDataConfigurationStorage();
			break;
		default:
			s_instance = new CMetaDataConfiguration();
			break;
		}

		return s_instance != nullptr ?
			s_instance->OnInitialize(flags) : false;
	}

	return false;
}

bool IMetaDataConfiguration::Destroy()
{
	if (s_instance != nullptr) {
		s_instance->OnDestroy();
	}
	wxDELETE(s_instance);
	return true;
}

//**************************************************************************************************
//*                                          ConfigMetadata										   *
//**************************************************************************************************

CMetaDataConfigurationFile::CMetaDataConfigurationFile(bool readOnly) : IMetaDataConfiguration(readOnly),
m_commonObject(nullptr), m_moduleManager(nullptr), m_configOpened(false)
{
	//create main metaObject
	m_commonObject = new CMetaObject();
	m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (m_commonObject->OnCreateMetaObject(this)) {
		m_moduleManager = new CModuleManagerConfiguration(this, m_commonObject);
		m_moduleManager->IncrRef();
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
		m_moduleManager->PrepareNames();
	}

	m_commonObject->PrepareNames();
	m_commonObject->IncrRef();

	wxASSERT(m_moduleManager);
}

CMetaDataConfigurationFile::~CMetaDataConfigurationFile()
{
	//delete module manager
	wxDELETE(m_moduleManager);

	//clear data 
	if (!ClearConfiguration()) {
		wxASSERT_MSG(false, "ClearConfiguration() == false");
	}

	//delete common metaObject
	wxDELETE(m_commonObject);
}

bool CMetaDataConfigurationFile::RunConfiguration(int flags)
{
	wxASSERT(!m_configOpened);

	if (!m_commonObject->OnBeforeRunMetaObject(flags)) {
		wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
		return false;
	}

	for (auto& obj : m_commonObject->GetObjects()) {
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
		for (auto& obj : m_commonObject->GetObjects()) {
			if (obj->IsDeleted())
				continue;
			if (!obj->OnAfterRunMetaObject(flags))
				return false;
			if (!RunChildMetadata(obj, flags, false))
				return false;
		}
		//if (!StartMainModule())
		//	return false;
		m_configOpened = true;
		return true;
	}

	return false;
}

bool CMetaDataConfigurationFile::RunChildMetadata(IMetaObject* metaParent, int flags, bool before)
{
	for (auto& obj : metaParent->GetObjects()) {

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

bool CMetaDataConfigurationFile::CloseConfiguration(int flags)
{
	wxASSERT(m_configOpened);

	//if (!ExitMainModule((flags & forceCloseFlag) != 0))
	//	return false;

	for (auto& obj : m_commonObject->GetObjects()) {

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

	if (!m_moduleManager->DestroyMainModule()) {
		return false;
	}

	for (auto& obj : m_commonObject->GetObjects()) {

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

	m_configOpened = false;
	return true;
}

bool CMetaDataConfigurationFile::CloseChildMetadata(IMetaObject* metaParent, int flags, bool before)
{
	for (auto& obj : metaParent->GetObjects()) {

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

bool CMetaDataConfigurationFile::ClearConfiguration()
{
	for (auto& obj : m_commonObject->GetObjects()) {

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

bool CMetaDataConfigurationFile::ClearChildMetadata(IMetaObject* metaParent)
{
	for (auto& obj : metaParent->GetObjects()) {

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

bool CMetaDataConfigurationFile::LoadFromFile(const wxString& strFileName)
{
	//close data 
	if (IsConfigOpen()) {
		if (!CloseConfiguration(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseConfiguration() == false");
			return false;

		}
	}

	//clear data 
	if (!ClearConfiguration()) {
		wxASSERT_MSG(false, "ClearConfiguration() == false");
		return false;
	}

	std::ifstream in(strFileName.ToStdWstring(), std::ios::in | std::ios::binary);

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

	//loading common metaData and child item
	if (!LoadCommonMetadata(g_metaCommonMetadataCLSID, readerData)) {
		//clear data 
		if (!ClearConfiguration()) {
			wxASSERT_MSG(false, "ClearConfiguration() == false");
		}
		return false;
	}

	return true;
}

bool CMetaDataConfigurationFile::LoadHeader(CMemoryReader& readerData)
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

bool CMetaDataConfigurationFile::LoadCommonMetadata(const class_identifier_t& clsid, CMemoryReader& readerData)
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
		if (!LoadConfiguration(clsid, *readerChildMemory, m_commonObject))
			return false;
	}

	return true;
}

bool CMetaDataConfigurationFile::LoadConfiguration(const class_identifier_t&, CMemoryReader& readerData, IMetaObject* metaParent)
{
	class_identifier_t clsid = 0;
	CMemoryReader* prevReaderMemory = nullptr;

	while (!readerData.eof())
	{
		CMemoryReader* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);

		if (!readerMemory)
			break;

		u64 meta_id = 0;
		CMemoryReader* prevReaderMetaMemory = nullptr;

		while (!readerMemory->eof())
		{
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);

			IMetaObject* newMetaObject = nullptr;
			CValue* ppParams[] = { metaParent };
			try {
				newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(clsid, ppParams, 1);
				newMetaObject->IncrRef();
			}
			catch (...) {
				return false;
			}

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

bool CMetaDataConfigurationFile::LoadChildMetadata(const class_identifier_t&, CMemoryReader& readerData, IMetaObject* metaParent)
{
	class_identifier_t clsid = 0;
	CMemoryReader* prevReaderMemory = nullptr;

	while (!readerData.eof())
	{
		CMemoryReader* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);

		if (!readerMemory)
			break;

		u64 meta_id = 0;
		CMemoryReader* prevReaderMetaMemory = nullptr;

		while (!readerMemory->eof())
		{
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);

			IMetaObject* newMetaObject = nullptr;
			CValue* ppParams[] = { metaParent };
			try {
				newMetaObject = CValue::CreateAndConvertObjectRef<IMetaObject>(clsid, ppParams, 1);
				newMetaObject->IncrRef();
			}
			catch (...) {
				return false;
			}

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
//*                                          ConfigMetadata                                        *
//**************************************************************************************************

bool CMetaDataConfiguration::OnInitialize(const int flags)
{
	m_metaGuid = Guid::newGuid();

	if (!CMetaDataConfigurationStorage::TableAlreadyCreated())
		return false;
	debugServerInit(flags);
	if (!LoadConfiguration()) return false;
	if (backend_mainFrame != nullptr) backend_mainFrame->OnInitializeConfiguration(GetConfigType());
	if ((flags & _app_start_create_debug_server_flag) != 0) debugServer->CreateServer(defaultHost, defaultDebuggerPort, true);
	return true;
}

bool CMetaDataConfiguration::OnDestroy()
{
	debugServerDestroy();
	if (backend_mainFrame != nullptr) backend_mainFrame->OnDestroyConfiguration(GetConfigType());
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

CMetaDataConfiguration::CMetaDataConfiguration(bool readOnly) : CMetaDataConfigurationFile(readOnly),
m_configNew(true)
{
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

bool CMetaDataConfigurationStorage::OnInitialize(const int flags)
{
	m_metaGuid = Guid::newGuid();

	if (!CMetaDataConfigurationStorage::TableAlreadyCreated()) {
		CMetaDataConfigurationStorage::CreateConfigTable();
		CMetaDataConfigurationStorage::CreateConfigSaveTable();
	}

	//Initialize debugger
	debugClientInit();
	if (!LoadConfiguration()) return false;
	if (backend_mainFrame != nullptr)
		backend_mainFrame->OnInitializeConfiguration(GetConfigType());
	return true;
}

bool CMetaDataConfigurationStorage::OnDestroy()
{
	debugClientDestroy();
	if (backend_mainFrame != nullptr)
		backend_mainFrame->OnDestroyConfiguration(GetConfigType());
	return true;
}

////////////////////////////////////////////////////////////////////////////////

CMetaDataConfigurationStorage::CMetaDataConfigurationStorage(bool readOnly) : CMetaDataConfiguration(readOnly),
m_configMetadata(new CMetaDataConfiguration(readOnly)), m_configSave(true) {
}

CMetaDataConfigurationStorage::~CMetaDataConfigurationStorage() {
	wxDELETE(m_configMetadata);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool CMetaDataConfigurationStorage::LoadConfiguration(int flags)
{
	if (m_configMetadata->LoadConfiguration(onlyLoadFlag)) {

		//close if opened
		if (CMetaDataConfiguration::IsConfigOpen()
			&& !CloseConfiguration(forceCloseFlag)) {
			return false;
		}

		if (CMetaDataConfiguration::LoadConfiguration()) {
			m_configSave = CompareMetadata(m_configMetadata);
			Modify(!m_configSave);
			if (m_configNew) SaveConfiguration(saveConfigFlag);
			m_configNew = false;
			return true;
		}
	}

	return false;
}

bool CMetaDataConfigurationStorage::SaveToFile(const wxString& strFileName)
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
	datafile.open(strFileName.ToStdWstring(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (writterData.pointer()), writterData.size());
	datafile.close();

	return true;
}

bool CMetaDataConfigurationStorage::SaveHeader(CMemoryWriter& writterData)
{
	CMemoryWriter writterMemory;
	writterMemory.w_u64(sign_metadata); //sign 
	writterMemory.w_stringZ(m_commonObject->GetDocPath()); //guid conf 

	writterData.w_chunk(eHeaderBlock, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CMetaDataConfigurationStorage::SaveCommonMetadata(const class_identifier_t& clsid, CMemoryWriter& writterData, int flags)
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

	if (!SaveConfiguration(clsid, writterChildMemory, flags))
		return false;

	writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
	writterMemory.w_chunk(m_commonObject->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());

	writterData.w_chunk(clsid, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CMetaDataConfigurationStorage::SaveConfiguration(const class_identifier_t&, CMemoryWriter& writterData, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	for (auto& obj : m_commonObject->GetObjects()) {
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
		if (!SaveChildMetadata(obj->GetClassType(), writterChildMemory, obj, flags)) {
			return false;
		}
		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(obj->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());
		writterData.w_chunk(obj->GetClassType(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

bool CMetaDataConfigurationStorage::SaveChildMetadata(const class_identifier_t&, CMemoryWriter& writterData, IMetaObject* metaParent, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	for (auto& obj : metaParent->GetObjects()) {
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
		if (!SaveChildMetadata(obj->GetClassType(), writterChildMemory, obj, flags)) {
			return false;
		}
		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(obj->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());
		writterData.w_chunk(obj->GetClassType(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

bool CMetaDataConfigurationStorage::DeleteCommonMetadata(const class_identifier_t& clsid)
{
	return DeleteMetadata(clsid);
}

bool CMetaDataConfigurationStorage::DeleteMetadata(const class_identifier_t& clsid)
{
	for (auto& obj : m_commonObject->GetObjects()) {
		if (obj->IsDeleted()) {
			if (!obj->DeleteMetaObject(this)) {
				return false;
			}
		}
		if (!DeleteChildMetadata(obj->GetClassType(), obj)) {
			return false;
		}
		if (obj->IsDeleted()) {
			m_commonObject->RemoveChild(obj);
			obj->DecrRef();
		}
	}

	return true;
}

bool CMetaDataConfigurationStorage::DeleteChildMetadata(const class_identifier_t& clsid, IMetaObject* metaParent)
{
	for (auto& obj : metaParent->GetObjects()) {
		if (obj->IsDeleted()) {
			if (!obj->DeleteMetaObject(this)) {
				return false;
			}
		}
		if (!DeleteChildMetadata(obj->GetClassType(), obj)) {
			return false;
		}
		if (obj->IsDeleted()) {
			metaParent->RemoveChild(obj);
			obj->DecrRef();
		}
	}

	return true;
}
