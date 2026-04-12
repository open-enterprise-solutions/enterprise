#include "metadataConfiguration.h"

#include "backend/debugger/debugClient.h"
#include "backend/debugger/debugServer.h"

#include "backend/backend_mainFrame.h"
#include "backend/appData.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////
ibMetaDataConfigurationBase* ibMetaDataConfigurationBase::ms_instance = nullptr;
//////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationBase::Initialize(ibRunMode mode, const int flags)
{
	if (ms_instance == nullptr) {

		switch (mode)
		{
		case eLAUNCHER_MODE: break;
		case eDESIGNER_MODE:
			ms_instance = new ibMetaDataConfigurationStorage();
			break;
		default:
			ms_instance = new ibMetaDataConfiguration();
			break;
		}

		return ms_instance != nullptr ?
			ms_instance->OnInitialize(flags) : false;
	}

	return false;
}

bool ibMetaDataConfigurationBase::Destroy()
{
	if (ms_instance != nullptr) {
		ms_instance->OnDestroy();
	}
	wxDELETE(ms_instance);
	return true;
}

#include <fstream>

bool ibMetaDataConfigurationBase::LoadConfigFromFile(const wxString& strFileName)
{
	std::ifstream in(strFileName.ToStdString(), std::ios::in | std::ios::binary);

	if (!in.is_open())
		return false;

	//go to end
	in.seekg(0, in.end);

	//get size of file
	std::streamsize fsize = in.tellg();

	//go to beginning
	in.seekg(0, in.beg);

	wxMemoryBuffer buffer(fsize);

	in.read((char*)buffer.GetWriteBuf(fsize), fsize);
	buffer.UngetWriteBuf(fsize);
	in.close();

	return LoadConfigFromBuffer(buffer);
}

bool ibMetaDataConfigurationBase::SaveConfigToFile(const wxString& strFileName)
{
	//common data
	wxMemoryBuffer buffer;

	if (!SaveConfigToBuffer(buffer))
		return false;

	std::ofstream datafile;
	datafile.open(strFileName.ToStdWstring(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (buffer.GetData()), buffer.GetBufSize());
	datafile.close();

	return true;
}

#include "backend/metaCollection/metaLanguageObject.h"

//**************************************************************************************************
//*                                          ConfigMetadata										   *
//**************************************************************************************************

ibMetaDataConfigurationFile::ibMetaDataConfigurationFile() : ibMetaDataConfigurationBase(),
m_commonObject(nullptr), m_moduleManager(nullptr), m_configOpened(false)
{
	//create main metaObject
	m_commonObject = new ibValueMetaObjectConfiguration();
	//m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (m_commonObject->OnCreateMetaObject(this, newObjectFlag)) {
		m_moduleManager = new ibValueModuleManagerConfiguration(this, m_commonObject);
		m_moduleManager->IncrRef();
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
		m_moduleManager->PrepareNames();
	}

	m_commonObject->PrepareNames();
	m_commonObject->IncrRef();

	{
		ibValue* ppParams[] = { m_commonObject };
		ibValueMetaObjectLanguage* commonLanguage =
			ibValue::CreateAndConvertObjectRef<ibValueMetaObjectLanguage>(g_metaLanguageCLSID, ppParams, 1);

		if (commonLanguage->OnCreateMetaObject(this, newObjectFlag)) {

			if (!commonLanguage->OnLoadMetaObject(this)) {
				wxASSERT_MSG(false, "commonLanguage->OnLoadMetaObject() == false");
			}

			commonLanguage->SetName(wxT("English"));
		}

		commonLanguage->PrepareNames();
		commonLanguage->IncrRef();

		m_commonObject->SetLanguage(commonLanguage->GetMetaID());
	}

	wxASSERT(m_moduleManager);
}

ibMetaDataConfigurationFile::~ibMetaDataConfigurationFile()
{
	//delete module manager
	wxDELETE(m_moduleManager);

	//clear data 
	if (!ClearDatabase()) {
		wxASSERT_MSG(false, "ClearDatabase() == false");
	}

	//delete common metaObject
	wxDELETE(m_commonObject);
}

////////////////////////////////////////////////////////////////////

wxString ibMetaDataConfigurationFile::GetLangCode() const
{
	if (m_commonObject != nullptr)
		return m_commonObject->GetLangCode();

	return wxT("");
}

bool ibMetaDataConfigurationFile::IsFullAccess() const
{
	bool access_right = true;

	for (const auto object : GetAnyArrayObject(g_metaRoleCLSID)) {
		access_right = false;
		break;
	}

	if (access_right)
		return true;

	if (m_commonObject != nullptr)
		return m_commonObject->AccessRight_Administration();

	return true;
}

////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationFile::RunDatabase(int flags)
{
	wxASSERT(!m_configOpened);

	if ((flags & loadConfigFlag) == 0)
		ibCompileCode::SetCodeStyle(m_commonObject->GetCompileSyntax());

	if (!m_commonObject->OnBeforeRunMetaObject(flags)) {
		wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
		return false;
	}

	for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

		auto child = m_commonObject->GetChild(idx);
		if (!m_commonObject->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted())
			continue;

		if (!child->OnBeforeRunMetaObject(flags))
			return false;

		if (!RunChildMetadata(child, flags, true))
			return false;
	}

	if (m_moduleManager->CreateMainModule()) {

		if (!m_commonObject->OnAfterRunMetaObject(flags)) {
			wxASSERT_MSG(false, "m_commonObject->OnBeforeRunMetaObject() == false");
			return false;
		}

		for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

			auto child = m_commonObject->GetChild(idx);
			if (!m_commonObject->FilterChild(child->GetClassType()))
				continue;

			if (child->IsDeleted())
				continue;

			if (!child->OnAfterRunMetaObject(flags))
				return false;

			if (!RunChildMetadata(child, flags, false))
				return false;
		}
		//if (!StartMainModule())
		//	return false;
		m_configOpened = true;
		return true;
	}

	return false;
}

bool ibMetaDataConfigurationFile::RunChildMetadata(ibValueMetaObject* object, int flags, bool before)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted())
			continue;

		if (before && !child->OnBeforeRunMetaObject(flags))
			return false;

		if (!before && !child->OnAfterRunMetaObject(flags))
			return false;

		if (!RunChildMetadata(child, flags, before))
			return false;
	}

	return true;
}

bool ibMetaDataConfigurationFile::CloseDatabase(int flags)
{
	wxASSERT(m_configOpened);

	//if (!ExitMainModule((flags & forceCloseFlag) != 0))
	//	return false;

	for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

		auto child = m_commonObject->GetChild(idx);
		if (!m_commonObject->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted())
			continue;

		if (!child->OnBeforeCloseMetaObject())
			return false;

		if (!CloseChildMetadata(child, (flags & forceCloseFlag) != 0, true))
			return false;
	}

	if (!m_commonObject->OnBeforeCloseMetaObject()) {
		wxASSERT_MSG(false, "m_commonObject->OnAfterCloseMetaObject() == false");
		return false;
	}

	if (!m_moduleManager->DestroyMainModule()) {
		return false;
	}

	for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

		auto child = m_commonObject->GetChild(idx);
		if (!m_commonObject->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted())
			continue;

		if (!child->OnAfterCloseMetaObject())
			return false;

		if (!CloseChildMetadata(child, (flags & forceCloseFlag) != 0, false))
			return false;
	}

	if (!m_commonObject->OnAfterCloseMetaObject()) {
		wxASSERT_MSG(false, "m_commonObject->OnAfterCloseMetaObject() == false");
		return false;
	}

	m_configOpened = false;
	return true;
}

bool ibMetaDataConfigurationFile::CloseChildMetadata(ibValueMetaObject* object, int flags, bool before)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted())
			continue;

		if (before && !child->OnBeforeCloseMetaObject())
			return false;

		if (!before && !child->OnAfterCloseMetaObject())
			return false;

		if (!CloseChildMetadata(child, flags, before))
			return false;
	}

	return true;
}

bool ibMetaDataConfigurationFile::ClearDatabase()
{
	for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

		auto child = m_commonObject->GetChild(idx);
		if (!m_commonObject->FilterChild(child->GetClassType()))
			continue;

		if (!child->OnDeleteMetaObject())
			return false;

		if (!ClearChildMetadata(child))
			return false;

		m_commonObject->RemoveChild(child);
		idx--;
	}

	if (!m_commonObject->OnDeleteMetaObject()) {
		wxASSERT_MSG(false, "m_commonObject->OnDeleteMetaObject() == false");
		return false;
	}

	return true;
}

bool ibMetaDataConfigurationFile::ClearChildMetadata(ibValueMetaObject* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;

		if (!child->OnDeleteMetaObject())
			return false;

		if (!ClearChildMetadata(child))
			return false;

		object->RemoveChild(child);
		idx--;
	}

	object->DecrRef();
	return true;
}

bool ibMetaDataConfigurationFile::LoadConfigFromBuffer(const wxMemoryBuffer& buffer)
{
	//close data 
	if (IsConfigOpen()) {
		if (!CloseDatabase(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseDatabase() == false");
			return false;

		}
	}

	//clear data 
	if (!ClearDatabase()) {
		wxASSERT_MSG(false, "ClearDatabase() == false");
		return false;
	}

	ibReaderMemory readerData(buffer.GetData(), buffer.GetBufSize());

	if (readerData.eof())
		return false;

	//Save header info 
	if (!LoadHeader(readerData))
		return false;

	//loading common metaData and child item
	if (!LoadCommonMetadata(g_metaCommonMetadataCLSID, readerData)) {
		//clear data 
		if (!ClearDatabase()) {
			wxASSERT_MSG(false, "ClearDatabase() == false");
		}
		return false;
	}

	return true;
}

bool ibMetaDataConfigurationFile::LoadHeader(ibReaderMemory& readerData)
{
	std::shared_ptr<ibReaderMemory> readerMemory(readerData.open_chunk(eHeaderBlock));

	if (!readerMemory)
		return false;

	u64 metaSign = readerMemory->r_u64();

	if (metaSign != sign_metadata)
		return false;

	wxString metaGuid;
	readerMemory->r_stringZ(metaGuid);

	return true;
}

bool ibMetaDataConfigurationFile::LoadCommonMetadata(const ibClassID& clsid, ibReaderMemory& readerData)
{
	std::shared_ptr<ibReaderMemory> readerMemory(readerData.open_chunk(clsid));

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	std::shared_ptr <ibReaderMemory> readerMetaMemory(readerMemory->open_chunk_iterator(meta_id));

	if (!readerMetaMemory)
		return true;

	std::shared_ptr <ibReaderMemory>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));

	//m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (!m_commonObject->LoadMetaObject(this, *readerDataMemory))
		return false;

	std::shared_ptr <ibReaderMemory> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));

	if (readerChildMemory) {
		if (!LoadDatabase(clsid, *readerChildMemory, m_commonObject))
			return false;
	}

	return true;
}

bool ibMetaDataConfigurationFile::LoadDatabase(const ibClassID&, ibReaderMemory& readerData, ibValueMetaObject* object)
{
	ibClassID clsid = 0;
	ibReaderMemory* prevReaderMemory = nullptr;

	while (!readerData.eof())
	{
		ibReaderMemory* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);

		if (!readerMemory)
			break;

		u64 meta_id = 0;
		ibReaderMemory* prevReaderMetaMemory = nullptr;

		while (!readerMemory->eof())
		{
			ibReaderMemory* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);

			ibValueMetaObject* newMetaObject = nullptr;
			ibValue* ppParams[] = { object };
			try {
				newMetaObject = ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(clsid, ppParams, 1);
				newMetaObject->IncrRef();
			}
			catch (...) {
				return false;
			}

			std::shared_ptr <ibReaderMemory> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory) {
				if (!LoadChildMetadata(clsid, *readerChildMemory, newMetaObject))
					return false;
			}

			std::shared_ptr <ibReaderMemory>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));

			if (!newMetaObject->LoadMetaObject(this, *readerDataMemory))
				return false;

			prevReaderMetaMemory = readerMetaMemory;
		}

		prevReaderMemory = readerMemory;
	};

	return true;
}

bool ibMetaDataConfigurationFile::LoadChildMetadata(const ibClassID&, ibReaderMemory& readerData, ibValueMetaObject* object)
{
	ibClassID clsid = 0;
	ibReaderMemory* prevReaderMemory = nullptr;

	while (!readerData.eof())
	{
		ibReaderMemory* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);

		if (!readerMemory)
			break;

		u64 meta_id = 0;
		ibReaderMemory* prevReaderMetaMemory = nullptr;

		while (!readerMemory->eof())
		{
			ibReaderMemory* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id, &*prevReaderMetaMemory);

			if (!readerMetaMemory)
				break;

			wxASSERT(clsid != 0);

			ibValueMetaObject* newMetaObject = nullptr;
			ibValue* ppParams[] = { object };
			try {
				newMetaObject = ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(clsid, ppParams, 1);
				newMetaObject->IncrRef();
			}
			catch (...) {
				return false;
			}

			std::shared_ptr <ibReaderMemory> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory) {
				if (!LoadChildMetadata(clsid, *readerChildMemory, newMetaObject))
					return false;
			}

			std::shared_ptr <ibReaderMemory>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
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

bool ibMetaDataConfiguration::OnInitialize(const int flags)
{
	m_metaGuid = ibGuid::newGuid();

	if (!ibMetaDataConfigurationStorage::TableAlreadyCreated())
		return false;

	debugServerInit(flags);

	if (!LoadDatabase())
		return false;

#pragma region language  
	// Check current language
	const ibValueMetaObject* foundedLanguage =
		ibMetaData::FindAnyObjectByFilter(appData->GetUserLanguageGuid(), g_metaLanguageCLSID);
	// Initialize localization engine  
	ibBackendLocalization::SetUserLanguage(foundedLanguage != nullptr ? appData->GetUserLanguageCode() : GetLangCode());
#pragma endregion 

	if (backend_mainFrame != nullptr)
		backend_mainFrame->OnInitializeConfiguration(GetConfigType());

	if ((flags & _app_start_create_debug_server_flag) != 0)
		debugServer->CreateServer(defaultHost, defaultDebuggerPort, true);

	return true;
}

bool ibMetaDataConfiguration::OnDestroy()
{
	debugServerDestroy();
	if (backend_mainFrame != nullptr) backend_mainFrame->OnDestroyConfiguration(GetConfigType());
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////

ibMetaDataConfiguration::ibMetaDataConfiguration() :
	ibMetaDataConfigurationFile(), m_configNew(true)
{
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

bool ibMetaDataConfigurationStorage::OnInitialize(const int flags)
{
	m_metaGuid = ibGuid::newGuid();

	if (!ibMetaDataConfigurationStorage::TableAlreadyCreated()) {
		ibMetaDataConfigurationStorage::CreateConfigTable();
		ibMetaDataConfigurationStorage::CreateConfigSaveTable();
		ibMetaDataConfigurationStorage::CreateConfigSequence();
	}

	// Initialize debugger
	debugClientInit();

	// Load database
	if (!LoadDatabase())
		return false;

#pragma region access
	if (!AccessRight_Administration()) {

		try {
			ibBackendCoreException::Error(_("Not enough access rights for this user!"));
		}
		catch (...) {
		}

		return false;
	}
#pragma endregion

#pragma region language  

	// Initialize localization engine 
	ibBackendLocalization::SetUserLanguage(GetLangCode());

#pragma endregion 

	if (backend_mainFrame != nullptr)
		backend_mainFrame->OnInitializeConfiguration(GetConfigType());

	return true;
}

bool ibMetaDataConfigurationStorage::OnDestroy()
{
	debugClientDestroy();

	if (backend_mainFrame != nullptr)
		backend_mainFrame->OnDestroyConfiguration(GetConfigType());

	return true;
}

////////////////////////////////////////////////////////////////////////////////

ibMetaDataConfigurationStorage::ibMetaDataConfigurationStorage() :
	ibMetaDataConfiguration(), m_configMetadata(new ibMetaDataConfiguration()) {
}

ibMetaDataConfigurationStorage::~ibMetaDataConfigurationStorage() {
	wxDELETE(m_configMetadata);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationStorage::LoadDatabase(int flags)
{
	if (m_configMetadata->LoadDatabase(onlyLoadFlag)) {

		//close if opened
		if (ibMetaDataConfiguration::IsConfigOpen()
			&& !CloseDatabase(forceCloseFlag)) {
			return false;
		}

		if (ibMetaDataConfiguration::LoadDatabase()) {
			Modify(!CompareMetadata(m_configMetadata));
			if (m_configNew)
				SaveDatabase(saveConfigFlag);
			m_configNew = false;
			return true;
		}
	}

	return false;
}

bool ibMetaDataConfigurationStorage::LoadDataFromBuffer(const wxMemoryBuffer& buffer)
{
	ibReaderMemory reader(buffer);

	//common data
	wxMemoryBuffer bufferData;

	if (reader.r_chunk(1, bufferData)) {

		ibValueMetaObject* commonObject = m_configMetadata->GetCommonMetaObject();
		wxASSERT(commonObject);

		ibReaderMemory* prevReaderMemory = nullptr;
		ibReaderMemory readerData(bufferData);

		while (!readerData.eof()) {

			u64 id = 0;

			ibReaderMemory* readerMemory = readerData.open_chunk_iterator(id, prevReaderMemory);
			if (!readerMemory)
				break;

			ibValueMetaObject* metaValue = commonObject->FindAnyObjectByFilter<ibValueMetaObject, ibMetaID>(id);
			if (metaValue != nullptr && !metaValue->LoadTableData(*readerMemory))
				return false;

			prevReaderMemory = readerMemory;
		};
	}

	//sequence 
	wxMemoryBuffer bufferSequence;

	if (reader.r_chunk(2, bufferSequence)) {
		ibReaderMemory readerSequence(bufferSequence);
		LoadSequenceFromBuffer(readerSequence);
	}

	return true;
}

bool ibMetaDataConfigurationStorage::SaveConfigToBuffer(wxMemoryBuffer& buffer)
{
	//common data
	ibWriterMemory writer;

	//Save header info 
	if (!SaveHeader(writer))
		return false;

	//Save common object
	if (!SaveCommonMetadata(g_metaCommonMetadataCLSID, writer, saveToFileFlag))
		return false;

	buffer = writer.buffer();
	return true;
}

bool ibMetaDataConfigurationStorage::SaveDataToBuffer(wxMemoryBuffer& buffer)
{
	ibWriterMemory writer;

	//common data
	ibWriterMemory writerData;

	ibValueMetaObject* commonObject = m_configMetadata->GetCommonMetaObject();
	wxASSERT(commonObject);

	for (unsigned int idx = 0; idx < commonObject->GetChildCount(); idx++) {

		auto child = commonObject->GetChild(idx);
		if (!commonObject->FilterChild(child->GetClassType()))
			continue;

		ibWriterMemory childWriter;
		if (!child->SaveTableData(childWriter))
			return false;
		writerData.w_chunk(child->GetMetaID(), childWriter.buffer());
	}

	writer.w_chunk(1, writerData.buffer());

	//sequence 
	ibWriterMemory writerSequence;
	if (SaveSequenceToBuffer(writerSequence))
		writer.w_chunk(2, writerSequence.buffer());

	buffer = writer.buffer();
	return true;
}

bool ibMetaDataConfigurationStorage::SaveHeader(ibWriterMemory& writerData)
{
	ibWriterMemory writerMemory;
	writerMemory.w_u64(sign_metadata); //sign 
	writerMemory.w_stringZ(m_commonObject->GetDocPath()); //guid conf 

	writerData.w_chunk(eHeaderBlock, writerMemory.pointer(), writerMemory.size());
	return true;
}

bool ibMetaDataConfigurationStorage::SaveCommonMetadata(const ibClassID& clsid, ibWriterMemory& writerData, int flags)
{
	//Save common object
	ibWriterMemory writerMemory;

	ibWriterMemory writerMetaMemory;
	ibWriterMemory writerDataMemory;

	if (!m_commonObject->SaveMetaObject(this, writerDataMemory, flags)) {
		return false;
	}

	writerMetaMemory.w_chunk(eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());

	ibWriterMemory writerChildMemory;

	if (!SaveDatabase(clsid, writerChildMemory, flags))
		return false;

	writerMetaMemory.w_chunk(eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());
	writerMemory.w_chunk(m_commonObject->GetMetaID(), writerMetaMemory.pointer(), writerMetaMemory.size());

	writerData.w_chunk(clsid, writerMemory.pointer(), writerMemory.size());
	return true;
}

bool ibMetaDataConfigurationStorage::SaveDatabase(const ibClassID&, ibWriterMemory& writerData, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

		auto child = m_commonObject->GetChild(idx);
		if (!m_commonObject->FilterChild(child->GetClassType()))
			continue;
		if (child->IsDeleted())
			continue;
		ibWriterMemory writerMemory;
		ibWriterMemory writerMetaMemory;
		ibWriterMemory writerDataMemory;
		if (!child->SaveMetaObject(this, writerDataMemory, flags)) {
			return false;
		}
		writerMetaMemory.w_chunk(eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());
		ibWriterMemory writerChildMemory;
		if (!SaveChildMetadata(child->GetClassType(), writerChildMemory, child, flags)) {
			return false;
		}
		writerMetaMemory.w_chunk(eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());
		writerMemory.w_chunk(child->GetMetaID(), writerMetaMemory.pointer(), writerMetaMemory.size());
		writerData.w_chunk(child->GetClassType(), writerMemory.pointer(), writerMemory.size());
	}

	return true;
}

bool ibMetaDataConfigurationStorage::SaveChildMetadata(const ibClassID&, ibWriterMemory& writerData, ibValueMetaObject* object, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;
		if (child->IsDeleted())
			continue;
		ibWriterMemory writerMemory;
		ibWriterMemory writerMetaMemory;
		ibWriterMemory writerDataMemory;
		if (!child->SaveMetaObject(this, writerDataMemory, flags)) {
			return false;
		}
		writerMetaMemory.w_chunk(eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());
		ibWriterMemory writerChildMemory;
		if (!SaveChildMetadata(child->GetClassType(), writerChildMemory, child, flags)) {
			return false;
		}
		writerMetaMemory.w_chunk(eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());
		writerMemory.w_chunk(child->GetMetaID(), writerMetaMemory.pointer(), writerMetaMemory.size());
		writerData.w_chunk(child->GetClassType(), writerMemory.pointer(), writerMemory.size());
	}

	return true;
}

bool ibMetaDataConfigurationStorage::DeleteCommonMetadata(const ibClassID& clsid)
{
	return DeleteMetadata(clsid);
}

bool ibMetaDataConfigurationStorage::DeleteMetadata(const ibClassID& clsid)
{
	for (unsigned int idx = 0; idx < m_commonObject->GetChildCount(); idx++) {

		auto child = m_commonObject->GetChild(idx);
		if (!m_commonObject->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted()) {
			if (!child->DeleteMetaObject(this)) {
				return false;
			}
		}
		if (!DeleteChildMetadata(child->GetClassType(), child)) {
			return false;
		}
		if (child->IsDeleted()) {
			m_commonObject->RemoveChild(child);
			child->DecrRef();
		}
	}

	return true;
}

bool ibMetaDataConfigurationStorage::DeleteChildMetadata(const ibClassID& clsid, ibValueMetaObject* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;

		if (child->IsDeleted()) {
			if (!child->DeleteMetaObject(this)) {
				return false;
			}
		}
		if (!DeleteChildMetadata(child->GetClassType(), child)) {
			return false;
		}
		if (child->IsDeleted()) {
			object->RemoveChild(child);
			child->DecrRef();
		}
	}

	return true;
}
