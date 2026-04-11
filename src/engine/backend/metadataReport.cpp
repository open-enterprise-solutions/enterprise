////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : external metaData - for dataProcessors, reports
////////////////////////////////////////////////////////////////////////////

#include "metadataReport.h"
#include "backend/appData.h"

ibMetaDataReport::ibMetaDataReport() : ibMetaData(),
m_commonObject(nullptr),
m_moduleManager(nullptr),
m_ownerMeta(nullptr),
m_configOpened(false),
m_version(version_oes_last)
{
	//create main metaObject
	m_commonObject = new ibValueMetaObjectExternalReport();
	m_commonObject->SetName(
		ibMetaData::GetNewName(g_metaExternalDataProcessorCLSID, nullptr, m_commonObject->GetClassName())
	);

	if (m_commonObject->OnCreateMetaObject(this, newObjectFlag)) {
		m_moduleManager = new ibValueModuleManagerExternalReport(this, m_commonObject);
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

ibMetaDataReport::ibMetaDataReport(ibMetaData* metaData, ibValueMetaObjectReport* srcReport) : ibMetaData(),
m_commonObject(srcReport),
m_ownerMeta(nullptr),
m_moduleManager(nullptr),
m_configOpened(false),
m_version(version_oes_last)
{
	if (srcReport == nullptr) {
		ibValueMetaObject* commonMetaObject = metaData->GetCommonMetaObject();
		wxASSERT(commonMetaObject);
		//create main metaObject
		m_commonObject = new ibValueMetaObjectReport();
		m_commonObject->SetName(
			ibMetaData::GetNewName(g_metaDataProcessorCLSID, nullptr, m_commonObject->GetClassName())
		);
		if (commonMetaObject != nullptr) {
			m_commonObject->SetParent(commonMetaObject);
			commonMetaObject->AddChild(m_commonObject);
		}
	}

	m_commonObject->IncrRef();
	m_commonObject->PrepareNames();

	m_ownerMeta = metaData;
}

ibMetaDataReport::~ibMetaDataReport()
{
	if (m_commonObject->IsExternalCreate()) {

		if (!m_moduleManager->DestroyMainModule()) {
			wxASSERT_MSG(false, "m_moduleManager->DestroyMainModule() == false");
		}

		//delete module manager
		if (m_moduleManager != nullptr) {
			m_moduleManager->DecrRef();
		}

		//clear data 
		if (!ClearDatabase()) {
			wxASSERT_MSG(false, "ClearDatabase() == false");
		}

		//delete common metaObject
		m_commonObject->DecrRef();
	}
}

bool ibMetaDataReport::LoadDatabase()
{
	return RunDatabase();
}

bool ibMetaDataReport::SaveDatabase()
{
	return true;
}

bool ibMetaDataReport::ClearDatabase()
{
	if (!ClearChildMetadata(m_commonObject))
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////

wxString ibMetaDataReport::GetLangCode() const
{
	return activeMetaData->GetLangCode();
}

////////////////////////////////////////////////////////////////////

bool ibMetaDataReport::ClearChildMetadata(ibValueMetaObject* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;

		if (!child->IsDeleted() && !child->OnDeleteMetaObject())
			return false;

		if (!ClearChildMetadata(child))
			return false;

		object->RemoveChild(child);
		idx--;
	}

	if (object != m_commonObject) 
		object->DecrRef();	
	
	return true;
}

bool ibMetaDataReport::RunDatabase(int flags)
{
	if (m_commonObject->IsExternalCreate()) {

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
	else if (!m_commonObject->IsExternalCreate()) {

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

bool ibMetaDataReport::RunChildMetadata(ibValueMetaObject* object, int flags, bool before)
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

bool ibMetaDataReport::CloseDatabase(int flags)
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

bool ibMetaDataReport::CloseChildMetadata(ibValueMetaObject* object, int flags, bool before)
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

#include <fstream>

bool ibMetaDataReport::LoadFromFile(const wxString& strFileName)
{
	if (!m_commonObject->IsExternalCreate()) {
		if (!m_commonObject->OnCreateMetaObject(m_ownerMeta, newObjectFlag))
			return false;
	}
	else if (m_commonObject->IsExternalCreate()) {
		//close data 
		if (m_configOpened && !CloseDatabase(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseDatabase() == false");
			return false;
		}

		//clear data 
		if (!ClearDatabase()) {
			wxASSERT_MSG(false, "ClearDatabase() == false");
			return false;
		}
	}

	std::ifstream in(strFileName.ToStdString(), std::ios::in | std::ios::binary);

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

	ibReaderMemory readerData(tempBuffer.GetData(), tempBuffer.GetBufSize());

	if (readerData.eof())
		return false;

	in.close();

	//Save header info 
	if (!LoadHeader(readerData))
		return false;

	m_fullPath = strFileName;

	//loading common metaData and child item
	if (!LoadCommonMetadata(g_metaExternalReportCLSID, readerData)) {
		if (m_commonObject->IsExternalCreate()) {
			//clear data 
			if (!ClearDatabase()) {
				wxASSERT_MSG(false, "ClearDatabase() == false");
			}
		}
		return false;
	}

	if (!m_commonObject->IsExternalCreate()) {
		m_commonObject->BuildNewName();
	}

	return LoadDatabase();
}

bool ibMetaDataReport::SaveToFile(const wxString& strFileName)
{
	//common data
	ibWriterMemory writerData;

	//Save header info 
	if (!SaveHeader(writerData))
		return false;

	m_fullPath = strFileName;

	//Save common object
	if (!SaveCommonMetadata(g_metaExternalReportCLSID, writerData, saveConfigFlag))
		return false;

	//Delete common object
	if (!DeleteCommonMetadata(g_metaExternalReportCLSID))
		return false;

	std::ofstream datafile;
	datafile.open(strFileName.ToStdString(), std::ios::binary);
	datafile.write(reinterpret_cast <char*> (writerData.pointer()), writerData.size());
	datafile.close();

	return true;
}

bool ibMetaDataReport::LoadHeader(ibReaderMemory& readerData)
{
	ibReaderMemory* readerMemory = readerData.open_chunk(eHeaderBlock);

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

bool ibMetaDataReport::LoadCommonMetadata(const ibClassID& clsid, ibReaderMemory& readerData)
{
	ibReaderMemory* readerMemory = readerData.open_chunk(clsid);

	if (!readerMemory)
		return false;

	u64 meta_id = 0;
	ibReaderMemory* readerMetaMemory = readerMemory->open_chunk_iterator(meta_id);

	if (!readerMetaMemory)
		return true;

	std::shared_ptr <ibReaderMemory> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
	if (readerChildMemory) {
		if (!LoadChildMetadata(clsid, *readerChildMemory, m_commonObject))
			return false;
	}

	std::shared_ptr <ibReaderMemory>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
	//m_commonObject->SetReadOnly(!m_metaReadOnly);
	if (!m_commonObject->LoadMetaObject(m_ownerMeta, *readerDataMemory))
		return false;

	if (!m_commonObject->IsExternalCreate()) {
		m_commonObject->ResetAll();
	}

	return true;
}

bool ibMetaDataReport::LoadChildMetadata(const ibClassID&, ibReaderMemory& readerData, ibValueMetaObject* object)
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

		while (!readerData.eof())
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
			if (!newMetaObject->LoadMetaObject(m_ownerMeta, *readerDataMemory))
				return false;

			if (!m_commonObject->IsExternalCreate()) {
				newMetaObject->ResetId();
			}

			prevReaderMetaMemory = readerMetaMemory;
		}

		prevReaderMemory = readerMemory;
	}

	return true;
}

bool ibMetaDataReport::SaveHeader(ibWriterMemory& writerData)
{
	ibWriterMemory writerMemory;
	writerMemory.w_u64(sign_dataReport); //sign 
	writerMemory.w_u32(m_version); // version 1 - DEFAULT
	writerMemory.w_stringZ(m_commonObject->GetDocPath()); //guid conf 

	writerData.w_chunk(eHeaderBlock, writerMemory.pointer(), writerMemory.size());
	return true;
}

bool ibMetaDataReport::SaveCommonMetadata(const ibClassID& clsid, ibWriterMemory& writerData, int flags)
{
	//Save common object
	ibWriterMemory writerMemory;

	ibWriterMemory writerMetaMemory;
	ibWriterMemory writerDataMemory;

	if (!m_commonObject->SaveMetaObject(m_ownerMeta, writerDataMemory, flags)) {
		return false;
	}

	writerMetaMemory.w_chunk(eDataBlock, writerDataMemory.pointer(), writerDataMemory.size());

	ibWriterMemory writerChildMemory;

	if (!SaveChildMetadata(clsid, writerChildMemory, m_commonObject, flags))
		return false;

	writerMetaMemory.w_chunk(eChildBlock, writerChildMemory.pointer(), writerChildMemory.size());
	writerMemory.w_chunk(m_commonObject->GetMetaID(), writerMetaMemory.pointer(), writerMetaMemory.size());

	writerData.w_chunk(clsid, writerMemory.pointer(), writerMemory.size());
	return true;
}

bool ibMetaDataReport::SaveChildMetadata(const ibClassID&, ibWriterMemory& writerData, ibValueMetaObject* object, int flags)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {

		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;
		ibWriterMemory writerMemory;
		if (child->IsDeleted())
			continue;
		ibWriterMemory writerMetaMemory;
		ibWriterMemory writerDataMemory;
		if (!child->SaveMetaObject(m_ownerMeta, writerDataMemory, flags)) {
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

bool ibMetaDataReport::DeleteCommonMetadata(const ibClassID& clsid)
{
	return DeleteChildMetadata(clsid, m_commonObject);
}

bool ibMetaDataReport::DeleteChildMetadata(const ibClassID& clsid, ibValueMetaObject* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
		auto child = object->GetChild(idx);
		if (!object->FilterChild(child->GetClassType()))
			continue;
		if (child->IsDeleted()) {
			if (!child->DeleteMetaObject(m_ownerMeta))
				return false;
			if (!DeleteChildMetadata(child->GetClassType(), child))
				return false;
			object->RemoveChild(child);
			child->DecrRef();
		}
		else {
			if (!DeleteChildMetadata(child->GetClassType(), child))
				return false;
		}
	}

	return true;
}