////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "backend/appData.h"

#include "backend/metaData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

wxIMPLEMENT_ABSTRACT_CLASS(ibValueMetaObject, ibValue);

#define metaBlock 0x200222
#define helpBlock 0x200224

//*****************************************************************************************
//*                                  MetaObject                                           *
//*****************************************************************************************

void ibValueMetaObject::ResetGuid()
{
	m_metaGuid = wxNewUniqueGuid;
}

void ibValueMetaObject::ResetId()
{
	if (m_metaData != nullptr) {
		m_metaId = m_metaData->GenerateNewID();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

ibBackendMetadataTree* ibValueMetaObject::GetMetaDataTree() const
{
	return m_metaData ? m_metaData->GetMetaTree() : nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObject::BuildNewName()
{
	const wxString& strName = GetName(); bool foundedName = false;
	std::vector<ibValueMetaObject*> array;
	if (m_parent != nullptr && m_parent->FillArrayObjectByFilter(array, { GetClassType() })) {
		for (const auto object : array) {
			if (object->GetParent() != GetParent())
				continue;
			if (object != this &&
				stringUtils::CompareString(strName, object->GetName())) {
				foundedName = true;
				break;
			}
		}
	}

	if (foundedName) {
		const wxString& metaPrevName = m_propertyName->GetValueAsString();
		size_t length = metaPrevName.length();
		while (length >= 0 && stringUtils::IsDigit(metaPrevName[--length]));
		const wxString& metaName = m_metaData->GetNewName(GetClassType(), GetParent(), metaPrevName.Left(length + 1));
		SetName(metaName);
		const wxString& metaPrevSynonym = m_propertySynonym->GetValueAsString();
		const wxString& metaSynonym = metaPrevSynonym.Length() > 0 ? stringUtils::GenerateSynonym(metaName) : wxString(wxEmptyString);
		SetSynonym(metaSynonym);
	}

	return !foundedName;
}

/////////////////////////////////////////////////////////////////////////////////////////

ibValueMetaObject::ibValueMetaObject(const wxString& strName, const wxString& synonym, const wxString& comment) : ibValue(ibValueTypes::TYPE_VALUE, true),
m_methodHelper(new ibValueMethodHelper()), m_metaData(nullptr), m_metaFlags(metaDefaultFlag), m_metaId(0)
{
	m_propertyName->SetValue(strName);
	m_propertySynonym->SetValue(synonym);
	m_propertyComment->SetValue(comment);
}

ibValueMetaObject::~ibValueMetaObject()
{
	wxDELETE(m_methodHelper);
}

bool ibValueMetaObject::LoadMeta(ibReaderMemory& dataReader)
{
	//Save meta version 
	(void)dataReader.r_u32(); //reserved 

	//Load unique guid 
	wxString strGuid;
	dataReader.r_stringZ(strGuid);
	m_metaGuid = strGuid;

	//Load meta id
	m_metaId = dataReader.r_u32();

	//Load standart fields
	m_propertyName->LoadData(dataReader);
	m_propertySynonym->LoadData(dataReader);
	m_propertyComment->LoadData(dataReader);

	//special info deleted 
	if (dataReader.r_u8()) {
		MarkAsDeleted();
	}

	//load interface 
	if (!LoadInterface(dataReader))
		return false;

	//load roles 
	if (!LoadRole(dataReader))
		return false;

	//load meta 
	wxMemoryBuffer meta_buffer;
	if (!dataReader.r_chunk(metaBlock, meta_buffer))
		return false;

	ibReaderMemory metaObjectReader(meta_buffer);
	metaObjectReader.r_u32(); //reserved flags
	if (!LoadData(metaObjectReader))
		return false;

	//load help 
	wxMemoryBuffer help_buffer;
	if (!dataReader.r_chunk(helpBlock, help_buffer))
		return false;
	
	ibReaderMemory helpReader(help_buffer);
	m_strHelpContent = helpReader.r_stringZ();
	return true;
}

bool ibValueMetaObject::SaveMeta(ibWriterMemory& dataWritter)
{
	//save meta version 
	dataWritter.w_u32(version_oes_last); //reserved 

	//save unique guid
	dataWritter.w_stringZ(m_metaGuid);

	//save meta id 
	dataWritter.w_u32(m_metaId);

	//save standart fields
	m_propertyName->SaveData(dataWritter);
	m_propertySynonym->SaveData(dataWritter);
	m_propertyComment->SaveData(dataWritter);

	//special info deleted
	dataWritter.w_u8(IsDeleted());

	//save interface 
	if (!SaveInterface(dataWritter))
		return false;

	//save roles 
	if (!SaveRole(dataWritter))
		return false;

	//save meta 
	ibWriterMemory metaObjectWritter;
	metaObjectWritter.w_u32(0); //reserved flags
	if (!SaveData(metaObjectWritter))
		return false;

	dataWritter.w_chunk(metaBlock, metaObjectWritter.buffer());

	//save help 
	ibWriterMemory helpWritter;
	helpWritter.w_stringZ(m_strHelpContent);
	dataWritter.w_chunk(helpBlock, helpWritter.buffer());
	return true;
}

bool ibValueMetaObject::LoadMetaObject(ibMetaData* metaData, ibReaderMemory& dataReader)
{
	m_metaData = metaData;

	if (!LoadMeta(dataReader))
		return false;

	if (!OnLoadMetaObject(metaData))
		return false;

	return true;
}

bool ibValueMetaObject::SaveMetaObject(ibMetaData* metaData, ibWriterMemory& dataWritter, int flags)
{
	bool saveToFile = (flags & saveToFileFlag) != 0;

	if (m_metaData != metaData)
		return false;

	if (!SaveMeta(dataWritter))
		return false;

	if (!saveToFile &&
		!OnSaveMetaObject(flags)) {
		return false;
	}

	return true;
}

bool ibValueMetaObject::DeleteMetaObject(ibMetaData* metaData)
{
	if (m_metaData != metaData)
		return false;

	return DeleteData();
}

bool ibValueMetaObject::CreateMetaTable(ibMetaDataConfiguration* srcMetaData, int flags)
{
	return CreateAndUpdateTableDB(srcMetaData, nullptr, flags);
}

bool ibValueMetaObject::UpdateMetaTable(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject)
{
	return CreateAndUpdateTableDB(srcMetaData, srcMetaObject, updateMetaTable);
}

bool ibValueMetaObject::DeleteMetaTable(ibMetaDataConfiguration* srcMetaData)
{
	return CreateAndUpdateTableDB(srcMetaData, this, deleteMetaTable);
}

bool ibValueMetaObject::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	GenerateGuid();
	wxASSERT(metaData);
	m_metaId = metaData->GenerateNewID();
	m_metaData = metaData;
#ifdef DEBUG  
	wxLogDebug(wxT("* Create metaData object %s with id %i"),
		GetClassName(), GetMetaID()
	);
#endif
	return true;
}

bool ibValueMetaObject::OnLoadMetaObject(ibMetaData* metaData)
{
	m_metaData = metaData;
	return true;
}

bool ibValueMetaObject::OnDeleteMetaObject()
{
	return true;
}

bool ibValueMetaObject::OnAfterCloseMetaObject()
{
	ibBackendMetadataTree* const metaTree = m_metaData->GetMetaTree();
	if (metaTree != nullptr)
		metaTree->CloseMetaObject(this);
	return true;
}

#pragma region interface_h
void ibValueMetaObject::DoSetInterface(const ibMetaID& id, const bool& val)
{
	m_metaData->Modify(true);
}
#pragma endregion
#pragma region role_h 
void ibValueMetaObject::DoSetRight(const ibRole* role, const bool& val)
{
	m_metaData->Modify(true);
}
#pragma endregion

bool ibValueMetaObject::IsFullAccess() const
{
	if (appData->DesignerMode())
		return true;

	return m_metaData->IsFullAccess();
}

ibRoleUserInfo ibValueMetaObject::GetUserRoleInfo() const
{
	ibRoleUserInfo roleInfo;
	for (auto role : appData->GetUserRoleArray())
		roleInfo.m_arrayRole.emplace_back(role.m_miRoleId);
	return roleInfo;
}

#define	headerBlock 0x002330
#define	dataBlock 0x002350
#define	childBlock 0x002370

bool ibValueMetaObject::Init()
{
	// always false
	return false;
}

bool ibValueMetaObject::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	ibValueMetaObject* parent = nullptr;
	if (paParams[0]->ConvertToValue(parent)) {
		const ibClassID& clsid = GetClassType();
		if (parent != nullptr) {
			SetParent(parent);
			parent->AddChild(this);
		}
		return parent != nullptr ?
			parent->FilterChild(clsid) : true;
	}

	return false;
}

bool ibValueMetaObject::IsEditable() const
{
	if (!IsEnabled() || IsDeleted())
		return false;

	ibBackendMetadataTree* const metaTree = m_metaData->GetMetaTree();
	if (metaTree != nullptr)
		return metaTree->IsEditable();

	return m_parent != nullptr ?
		m_parent->IsEditable() : true;
}

bool ibValueMetaObject::CompareObject(const ibValueMetaObject* compareObject) const
{
#pragma region _compare_fill_h_
	class ibControlComparator {
	public:

		static bool CompareObject(
			const ibValueMetaObject* compareObject1,
			const ibValueMetaObject* compareObject2
		)
		{
			if (compareObject2 == nullptr)
				return false;

			if (compareObject1->GetClassType() != compareObject2->GetClassType())
				return false;

			if (compareObject1->GetMetaID() != compareObject2->GetMetaID())
				return false;

			for (unsigned int idx = 0; idx < compareObject1->GetPropertyCount(); idx++) {

				const ibProperty* propDst = compareObject1->GetProperty(idx);
				wxASSERT(propDst);

				const ibProperty* propSrc = compareObject2->GetProperty(propDst->GetName());

				if (propSrc == nullptr)
					return false;

				if (propDst->GetValue() != propSrc->GetValue())
					return false;
			}

			return (compareObject1->GetPropertyCount() == compareObject2->GetPropertyCount() && compareObject1->GetEventCount() == compareObject2->GetEventCount()) &&
				compareObject1->GetChildCount() == compareObject2->GetChildCount();
		}
	};

#pragma endregion 

	return ibControlComparator::CompareObject(this, compareObject);
}

bool ibValueMetaObject::CopyObject(ibWriterMemory& writer) const
{
#pragma region _copy_guard_h_

	class ibControlCopyGuard {

		static void Generate(const ibValueMetaObject* copyObject) {
			for (unsigned int idx = 0; idx < copyObject->GetChildCount(); idx++)
				Generate(copyObject->GetChild(idx));
			copyObject->m_metaCopyGuid = wxNewUniqueGuid;
		}

		static void Erase(const ibValueMetaObject* copyObject) {
			for (unsigned int idx = 0; idx < copyObject->GetChildCount(); idx++)
				Erase(copyObject->GetChild(idx));
			copyObject->m_metaCopyGuid = wxNullGuid;
		}

	public:

		ibControlCopyGuard(const ibValueMetaObject* copyObject) : m_copyObject(copyObject) { Generate(m_copyObject); }
		~ibControlCopyGuard() { Erase(m_copyObject); }

	protected:
		const ibValueMetaObject* m_copyObject = nullptr;
	};

	ibControlCopyGuard controlCopyGuard(this);

#pragma endregion 

	wxASSERT(m_metaCopyGuid.isValid());

#pragma region _copy_fill_h_

	class ibControlMemoryWriter {
	public:

		static bool CopyObject(const ibValueMetaObject* copyObject, ibWriterMemory& writer)
		{
			ibWriterMemory writerHeaderMemory;
			writerHeaderMemory.w_s32(copyObject->m_metaData->GetVersion());
			writerHeaderMemory.w_stringZ(copyObject->m_metaCopyGuid);
			writer.w_chunk(headerBlock, writerHeaderMemory.pointer(), writerHeaderMemory.size());

			ibWriterMemory writerChildMemory;

			for (const auto object : copyObject->m_children) {

				if (!copyObject->FilterChild(object->GetClassType()))
					continue;
				if (object->IsDeleted())
					continue;
				ibWriterMemory writerMemory;
				if (!CopyObject(object, writerMemory))
					return false;

				writerChildMemory.w_chunk(object->GetClassType(), writerMemory.pointer(), writerMemory.size());
			}

			writer.w_chunk(childBlock, writerChildMemory.pointer(), writerChildMemory.size());

			ibWriterMemory writerDataMemory;
			
			if (!copyObject->CopyProperty(writerDataMemory))
				return false;

			if (!copyObject->SaveInterface(writerDataMemory))
				return false;

			if (!copyObject->SaveRole(writerDataMemory))
				return false;

			writer.w_chunk(dataBlock, writerDataMemory.pointer(), writerDataMemory.size());
			return true;
		}
	};

#pragma endregion 

	return ibControlMemoryWriter::CopyObject(this, writer);
}

bool ibValueMetaObject::PasteObject(ibReaderMemory& reader)
{
#pragma region _paste_fill_h_

	class ibControlMemoryReader {

		static bool PasteObject(ibValueMetaObject* pasteObject, ibReaderMemory& reader)
		{
			ibMetaData* metaData = pasteObject->GetMetaData();

			std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

			/*const ibVersionID& version =*/ readerHeaderMemory->r_s32();
			pasteObject->m_metaGuid = readerHeaderMemory->r_stringZ();

			//and running initialization
			if (!pasteObject->OnBeforeRunMetaObject(onlyLoadFlag))
				return false;


			std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));

			if (!pasteObject->PasteProperty(*readerDataMemory))
				return false;

			pasteObject->BuildNewName();

			pasteObject->LoadInterface(*readerDataMemory);
			pasteObject->LoadRole(*readerDataMemory);

			if (!pasteObject->OnAfterRunMetaObject(onlyLoadFlag))
				return false;

			std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
			if (readerChildMemory != nullptr) {
				ibReaderMemory* prevReaderMemory = nullptr;
				do {
					ibClassID clsid = 0;
					ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
					if (readerMemory == nullptr)
						break;
					if (clsid > 0) {
						ibValueMetaObject* metaObject = metaData->CreateMetaObject(clsid, pasteObject, false);
						if (metaObject != nullptr && !PasteObject(metaObject, *readerMemory)) {
							wxDELETE(metaObject);
							return false;
						}
					}
					prevReaderMemory = readerMemory;
				} while (true);
			}

			return true;
		}

	public:

		static bool PasteAndRunObject(ibValueMetaObject* pasteObject, ibReaderMemory& reader)
		{
			ibMetaData* metaData = pasteObject->GetMetaData();

			std::shared_ptr <ibReaderMemory>readerHeaderMemory(reader.open_chunk(headerBlock));

			/*const ibVersionID& version =*/ readerHeaderMemory->r_s32();
			/*pasteObject->m_metaGuid =*/ readerHeaderMemory->r_stringZ();

			//and running initialization
			if (!pasteObject->OnBeforeRunMetaObject(onlyLoadFlag))
				return false;

			std::shared_ptr <ibReaderMemory>readerDataMemory(reader.open_chunk(dataBlock));

			if (!pasteObject->PasteProperty(*readerDataMemory))
				return false;

			pasteObject->BuildNewName();

			pasteObject->LoadInterface(*readerDataMemory);
			pasteObject->LoadRole(*readerDataMemory);

			if (!pasteObject->OnAfterRunMetaObject(onlyLoadFlag))
				return false;

			std::shared_ptr <ibReaderMemory> readerChildMemory(reader.open_chunk(childBlock));
			if (readerChildMemory != nullptr) {
				ibReaderMemory* prevReaderMemory = nullptr;
				do {
					ibClassID clsid = 0;
					ibReaderMemory* readerMemory = readerChildMemory->open_chunk_iterator(clsid, &*prevReaderMemory);
					if (readerMemory == nullptr)
						break;
					if (clsid > 0) {
						ibValueMetaObject* metaObject = metaData->CreateMetaObject(clsid, pasteObject, false);
						if (metaObject != nullptr && !PasteObject(metaObject, *readerMemory)) {
							wxDELETE(metaObject);
							return false;
						}
					}
					prevReaderMemory = readerMemory;
				} while (true);
			}

			return pasteObject->OnReloadMetaObject();
		}
	};

#pragma endregion 

	return ibControlMemoryReader::PasteAndRunObject(this, reader);
}

bool ibValueMetaObject::ChangeChildPosition(ibValueMetaObject* object, unsigned int pos)
{
	m_metaData->Modify(true);
	return ibPropertyObjectHelper::ChangeChildPosition(object, pos);
}

wxString ibValueMetaObject::GetModuleName() const
{
	ibValueMetaObject* parent = GetParent();
	//wxASSERT(parent);
	if (parent != nullptr) {
		return parent->GetName() + wxT(": ") + GetName();
	}
	return GetName();
}

wxString ibValueMetaObject::GetFullName() const
{
	wxString strFullName;

	ibValueMetaObject* parent = GetParent();

	if (parent != nullptr && g_metaModuleCLSID != GetClassType() && g_metaManagerCLSID != GetClassType())
		strFullName = strFullName + GetClassName() + '.' + GetName();
	else
		strFullName = GetName();

	while (parent != nullptr) {
		if (g_metaCommonMetadataCLSID != parent->GetClassType()) {
			strFullName = parent->GetClassName() + '.' +
				parent->GetName() + '.' +
				strFullName;
		}
		parent = parent->GetParent();
	}

	return strFullName;
}

wxString ibValueMetaObject::GetFileName() const
{
	return m_metaData->GetFileName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void ibValueMetaObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	for (unsigned idx = 0; idx < ibPropertyObject::GetPropertyCount(); idx++) {
		ibProperty* property = ibPropertyObject::GetProperty(idx);
		if (property == nullptr) continue;
		m_methodHelper->AppendProp(property->GetName(), true, false, idx);
	}
}

bool ibValueMetaObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	ibProperty* property = GetPropertyByIndex(lPropNum);
	if (property != nullptr) return property->SetDataValue(varPropVal);
	return false;
}

bool ibValueMetaObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibProperty* property = GetPropertyByIndex(lPropNum);
	if (property != nullptr) return property->GetDataValue(pvarPropVal);
	return false;
}

ibRestructureInfo s_restructureInfo;