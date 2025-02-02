////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : external metaData - for dataProcessors, reports
////////////////////////////////////////////////////////////////////////////

#include "metadataReport.h"
#include "backend/appData.h"

CMetaDataReport::CMetaDataReport() : IMetaData(),
m_commonObject(nullptr),
m_ownerMeta(nullptr),
m_moduleManager(nullptr),
m_configOpened(false),
m_version(version_oes_last)
{
	//create main metaObject
	m_commonObject = new CMetaObjectReportExternal();
	m_commonObject->SetName(
		IMetaData::GetNewName(g_metaExternalDataProcessorCLSID, nullptr, m_commonObject->GetClassName())
	);
	m_commonObject->SetReadOnly(!m_metaReadOnly);

	if (m_commonObject->OnCreateMetaObject(this)) {
		m_moduleManager = new CModuleManagerExternalReport(this, m_commonObject);
		m_moduleManager->IncrRef();
		if (!m_commonObject->OnLoadMetaObject(this)) {
			wxASSERT_MSG(false, "m_commonObject->OnLoadMetaObject() == false");
		}
		m_moduleManager->PrepareNames();
	}

	m_commonObject->PrepareNames();
	m_commonObject->IncrRef();

	wxASSERT(m_moduleManager);
	m_ownerMeta = this;
}

CMetaDataReport::CMetaDataReport(IMetaData* metaData, CMetaObjectReport* srcReport) : IMetaData(),
m_commonObject(srcReport),
m_ownerMeta(nullptr),
m_moduleManager(nullptr),
m_configOpened(false),
m_version(version_oes_last)
{
	if (srcReport == nullptr) {
		IMetaObject* commonMetaObject = metaData->GetCommonMetaObject();
		wxASSERT(commonMetaObject);
		//create main metaObject
		m_commonObject = new CMetaObjectReport();
		m_commonObject->SetName(
			IMetaData::GetNewName(g_metaDataProcessorCLSID, nullptr, m_commonObject->GetClassName())
		);
		if (commonMetaObject != nullptr) {
			m_commonObject->SetParent(commonMetaObject);
			commonMetaObject->AddChild(m_commonObject);
		}
		if (commonMetaObject != nullptr) {
			commonMetaObject->AppendChild(m_commonObject);
		}
	}

	m_commonObject->IncrRef();
	m_commonObject->PrepareNames();

	m_ownerMeta = metaData;
}

CMetaDataReport::~CMetaDataReport()
{
	if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		if (!m_moduleManager->DestroyMainModule()) {
			wxASSERT_MSG(false, "m_moduleManager->DestroyMainModule() == false");
		}
		//delete module manager
		if (m_moduleManager != nullptr) {
			m_moduleManager->DecrRef();
		}
		//clear data 
		if (!ClearConfiguration()) {
			wxASSERT_MSG(false, "ClearConfiguration() == false");
		}
	}

	if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		//delete common metaObject
		m_commonObject->DecrRef();
	}
}

bool CMetaDataReport::LoadConfiguration()
{
	return RunConfiguration();
}

bool CMetaDataReport::SaveConfiguration()
{
	return true;
}

bool CMetaDataReport::ClearConfiguration()
{
	if (!ClearChildMetadata(m_commonObject))
		return false;

	return true;
}

bool CMetaDataReport::ClearChildMetadata(IMetaObject* metaParent)
{
	for (auto& obj : metaParent->GetObjects()) {
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

bool CMetaDataReport::RunConfiguration(int flags)
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
			m_configOpened = true;
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

bool CMetaDataReport::RunChildMetadata(IMetaObject* metaParent, int flags, bool before)
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

bool CMetaDataReport::CloseConfiguration(int flags)
{
	wxASSERT(m_configOpened);

	if (!ExitMainModule((flags & forceCloseFlag) != 0))
		return false;

	if (!m_commonObject->IsDeleted()) {
		if (!CloseChildMetadata(m_commonObject, (flags & forceCloseFlag) != 0, true)) {
			return false;
		}
	}
	m_commonObject->OnBeforeCloseMetaObject();
	if (!CloseChildMetadata(m_commonObject, (flags & forceCloseFlag) != 0, false))
		return false;
	m_commonObject->OnAfterCloseMetaObject();
	m_configOpened = false;
	return true;
}

bool CMetaDataReport::CloseChildMetadata(IMetaObject* metaParent, int flags, bool before)
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

#include <fstream>

bool CMetaDataReport::LoadFromFile(const wxString& strFileName)
{
	if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
		if (!m_commonObject->OnCreateMetaObject(m_ownerMeta))
			return false;
	}
	else if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
		//close data 
		if (m_configOpened && !CloseConfiguration(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseConfiguration() == false");
			return false;
		}

		//clear data 
		if (!ClearConfiguration()) {
			wxASSERT_MSG(false, "ClearConfiguration() == false");
			return false;
		}
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

	m_fullPath = strFileName;

	//loading common metaData and child item
	if (!LoadCommonMetadata(g_metaExternalReportCLSID, readerData)) {
		if (m_commonObject->GetObjectMode() == METAOBJECT_EXTERNAL) {
			//clear data 
			if (!ClearConfiguration()) {
				wxASSERT_MSG(false, "ClearConfiguration() == false");
			}
		}
		return false;
	}

	if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
		m_commonObject->BuildNewName();
	}

	return LoadConfiguration();
}

bool CMetaDataReport::SaveToFile(const wxString& strFileName)
{
	//common data
	CMemoryWriter writterData;

	//Save header info 
	if (!SaveHeader(writterData))
		return false;

	m_fullPath = strFileName;

	//Save common object
	if (!SaveCommonMetadata(g_metaExternalReportCLSID, writterData))
		return false;

	//Delete common object
	if (!DeleteCommonMetadata(g_metaExternalReportCLSID))
		return false;

	std::ofstream datafile;
	datafile.open(strFileName.ToStdWstring(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (writterData.pointer()), writterData.size());
	datafile.close();

	return true;
}

bool CMetaDataReport::LoadHeader(CMemoryReader& readerData)
{
	CMemoryReader* readerMemory = readerData.open_chunk(eHeaderBlock);

	if (!readerMemory)
		return false;

	u64 metaSign = readerMemory->r_u64();

	if (metaSign != sign_dataReport)
		return false;

	m_version = readerMemory->r_u32();

	wxString metaGuid;
	readerMemory->r_stringZ(metaGuid);

	readerMemory->close();
	return true;
}

bool CMetaDataReport::LoadCommonMetadata(const class_identifier_t& clsid, CMemoryReader& readerData)
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

bool CMetaDataReport::LoadChildMetadata(const class_identifier_t&, CMemoryReader& readerData, IMetaObject* metaParent)
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

		while (!readerData.eof())
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
			if (!newMetaObject->LoadMetaObject(m_ownerMeta, *readerDataMemory))
				return false;

			if (m_commonObject->GetObjectMode() == METAOBJECT_NORMAL) {
				newMetaObject->ResetId();
			}

			prevReaderMetaMemory = readerMetaMemory;
		}

		prevReaderMemory = readerMemory;
	}

	return true;
}

bool CMetaDataReport::SaveHeader(CMemoryWriter& writterData)
{
	CMemoryWriter writterMemory;
	writterMemory.w_u64(sign_dataReport); //sign 
	writterMemory.w_u32(m_version); // version 1 - DEFAULT
	writterMemory.w_stringZ(m_commonObject->GetDocPath()); //guid conf 

	writterData.w_chunk(eHeaderBlock, writterMemory.pointer(), writterMemory.size());
	return true;
}

bool CMetaDataReport::SaveCommonMetadata(const class_identifier_t& clsid, CMemoryWriter& writterData, bool saveToFile)
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

bool CMetaDataReport::SaveChildMetadata(const class_identifier_t&, CMemoryWriter& writterData, IMetaObject* metaParent, bool saveToFile)
{
	for (auto& obj : metaParent->GetObjects())
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

		if (!SaveChildMetadata(obj->GetClassType(), writterChildMemory, obj, saveToFile)) {
			return false;
		}

		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(obj->GetMetaID(), writterMetaMemory.pointer(), writterMetaMemory.size());

		writterData.w_chunk(obj->GetClassType(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

bool CMetaDataReport::DeleteCommonMetadata(const class_identifier_t& clsid)
{
	return DeleteChildMetadata(clsid, m_commonObject);
}

bool CMetaDataReport::DeleteChildMetadata(const class_identifier_t& clsid, IMetaObject* metaParent)
{
	for (auto& obj : metaParent->GetObjects()) {
		if (obj->IsDeleted()) {
			if (!obj->DeleteMetaObject(m_ownerMeta))
				return false;
			if (!DeleteChildMetadata(obj->GetClassType(), obj))
				return false;
			metaParent->RemoveChild(obj);
			obj->DecrRef();
		}
		else {
			if (!DeleteChildMetadata(obj->GetClassType(), obj))
				return false;
		}
	}

	return true;
}