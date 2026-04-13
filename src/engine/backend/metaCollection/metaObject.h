#ifndef __META_OBJECT_H__
#define __META_OBJECT_H__

#include "backend/propertyManager/propertyManager.h"

#include "backend/backend_metatree.h"
#include "backend/metaCtor.h"

#include "backend/restructureInfo.h"

#include "backend/interfaceHelper.h"
#include "backend/roleHelper.h"

//*******************************************************************************
class BACKEND_API ibMetaData;
//*******************************************************************************
//*                          define commom clsid                                *
//*******************************************************************************

//COMMON METADATA
const ibClassID g_metaCommonMetadataCLSID = string_to_clsid("MD_MTD");

//COMMON OBJECTS
const ibClassID g_metaCommonModuleCLSID = string_to_clsid("MD_CMOD");
const ibClassID g_metaCommonFormCLSID = string_to_clsid("MD_CFRM");
const ibClassID g_metaCommonTemplateCLSID = string_to_clsid("MD_CTMP");

const ibClassID g_metaRoleCLSID = string_to_clsid("MD_ROLE");
const ibClassID g_metaInterfaceCLSID = string_to_clsid("MD_SSYST");
const ibClassID g_metaPictureCLSID = string_to_clsid("MD_PICTR");
const ibClassID g_metaLanguageCLSID = string_to_clsid("MD_LANG");

//ADVANCED OBJECTS
const ibClassID g_metaAttributeCLSID = string_to_clsid("MD_ATTR");
const ibClassID g_metaFormCLSID = string_to_clsid("MD_FRM");
const ibClassID g_metaTemplateCLSID = string_to_clsid("MD_TMPL");
const ibClassID g_metaModuleCLSID = string_to_clsid("MD_MOD");
const ibClassID g_metaManagerCLSID = string_to_clsid("MD_MNGR");
const ibClassID g_metaTableCLSID = string_to_clsid("MD_TBL");
const ibClassID g_metaEnumCLSID = string_to_clsid("MD_ENUM");
const ibClassID g_metaDimensionCLSID = string_to_clsid("MD_DMNT");
const ibClassID g_metaResourceCLSID = string_to_clsid("MD_RESS");

//SPECIAL OBJECTS
const ibClassID g_metaPredefinedAttributeCLSID = string_to_clsid("MD_DATT");

//MAIN OBJECTS
const ibClassID g_metaConstantCLSID = string_to_clsid("MD_CONS");
const ibClassID g_metaCatalogCLSID = string_to_clsid("MD_CAT");
const ibClassID g_metaDocumentCLSID = string_to_clsid("MD_DOC");
const ibClassID g_metaEnumerationCLSID = string_to_clsid("MD_ENM");
const ibClassID g_metaDataProcessorCLSID = string_to_clsid("MD_DPR");
const ibClassID g_metaReportCLSID = string_to_clsid("MD_RPT");
const ibClassID g_metaInformationRegisterCLSID = string_to_clsid("MD_INFR");
const ibClassID g_metaAccumulationRegisterCLSID = string_to_clsid("MD_ACCR");

//ACCOUNTING OBJECTS
const ibClassID g_metaChartOfCharacteristicTypesCLSID = string_to_clsid("MD_CHRC");
const ibClassID g_metaChartOfAccountsCLSID = string_to_clsid("MD_CHOA");
const ibClassID g_metaAccountingRegisterCLSID = string_to_clsid("MD_AREG");

// EXTERNAL
const ibClassID g_metaExternalDataProcessorCLSID = string_to_clsid("MD_EDPR");
const ibClassID g_metaExternalReportCLSID = string_to_clsid("MD_ERPT");

//*******************************************************************************
//*                             ibValueMetaObject                                *
//*******************************************************************************

#define defaultMetaID 1000

//flags metaobject event 
enum metaObjectFlags {

	defaultFlag = 0x0000,
	onlyLoadFlag = 0x0001,

	loadConfigFlag = 0x0002,
	saveConfigFlag = 0x0004,

	newObjectFlag = 0x0008,
	forceRunFlag = 0x0010,
	forceCloseFlag = 0x0020,

	loadFileFlag = 0x0040,
	saveToFileFlag = 0x0080,

	copyObjectFlag = 0x0100,
	pasteObjectFlag = 0x0200,
};

#define rt_ref_chunk 0x800060

//flags metaobject 
#define metaDeletedFlag 0x0001000
#define metaCanSaveFlag 0x0002000
#define metaDisableFlag 0x0008000

#define metaDefaultFlag metaCanSaveFlag

//flags save
#define createMetaTable  0x0001000
#define updateMetaTable	 0x0002000	
#define deleteMetaTable	 0x0004000	

#define repairMetaTable  0x0008000

class BACKEND_API ibValueMetaObject :

	public ibValue,

	public ibPropertyObjectHelper<ibValueMetaObject>,
	public ibAccessObject, public ibInterfaceObject {

	wxDECLARE_ABSTRACT_CLASS(ibValueMetaObject);

public:

	// get object name as string 
	bool GetObjectNameAsString(wxString& result) const {
		return m_propertyName->GetValueAsString(result);
	}

	//system attributes 
	ibMetaID GetMetaID() const { return m_metaId; }
	void SetMetaID(const ibMetaID& id) { m_metaId = id; }

	wxString GetName() const { return m_propertyName->GetValueAsString(); }
	void SetName(const wxString& strName) { m_propertyName->SetValue(strName); }

	virtual wxString GetSynonym() const {
		return !m_propertySynonym->IsEmptyProperty() ?
			m_propertySynonym->GetValueAsTranslateString() : stringUtils::GenerateSynonym(GetName());
	}
	virtual void SetSynonym(const wxString& synonym) { m_propertySynonym->SetValue(synonym); }

	wxString GetComment() const { return m_propertyComment->GetValueAsString(); }
	void SetComment(const wxString& comment) { m_propertyComment->SetValue(comment); }

	wxString GetHelpContent() const { return m_strHelpContent; }
	void SetHelpContent(const wxString& strHelpContent) { m_strHelpContent = strHelpContent; }

	virtual void SetMetaData(ibMetaData* metaData) { m_metaData = metaData; }
	virtual ibMetaData* GetMetaData() const override { return m_metaData; }

	void ResetGuid();
	void ResetId();

	void ResetAll() {
		ResetGuid(); ResetId();
	}

	void GenerateGuid() {
		wxASSERT(!m_metaGuid.isValid());
		if (!m_metaGuid.isValid()) {
			ResetGuid();
		}
	}

	inline bool CompareId(const ibMetaID& id) const { return m_metaId == id; }
	inline bool CompareGuid(const ibGuid& guid) const { return m_metaGuid == guid; }

	operator ibMetaID() const { return m_metaId; }

	ibBackendMetadataTree* GetMetaDataTree() const;

public:

	bool IsAllowed() const {
		return IsEnabled()
			&& !IsDeleted();
	}

	bool IsEnabled() const {
		return (m_metaFlags & metaDisableFlag) == 0;
	}

	bool IsDeleted() const {
		return (m_metaFlags & metaDeletedFlag) != 0;
	}

public:

	void MarkAsDeleted() {
		m_metaFlags |= metaDeletedFlag;
	}

public:

	void SetFlag(int flag) {
		m_metaFlags |= flag;
	}

	void ClearFlag(int flag) {
		m_metaFlags &= ~(flag);
	}

public:

	bool BuildNewName();

	ibValueMetaObject(
		const wxString& strName = wxEmptyString,
		const wxString& synonym = wxEmptyString,
		const wxString& comment = wxEmptyString
	);

	virtual ~ibValueMetaObject();

	//system override 
	virtual int GetComponentType() const final { return COMPONENT_TYPE_METADATA; }

	virtual wxString GetClassName() const final { return ibValue::GetClassName(); }
	virtual wxString GetObjectTypeName() const final { return ibValue::GetClassName(); }

	ibGuid GetGuid() const { return m_metaGuid; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool IsCopyMode() const {
		return m_metaCopyGuid.isValid();
	}

	bool IsPasteMode() const {
		return m_metaPasteGuid.isValid();
	}

	void SetCommonGuid(const ibGuid& guid) {
		m_metaCopyGuid.reset(); m_metaPasteGuid.reset(); m_metaGuid = guid;
	}

	ibGuid GetCommonGuid() const {

		if (m_metaPasteGuid.isValid())
			return m_metaPasteGuid;

		if (m_metaCopyGuid.isValid())
			return m_metaCopyGuid;

		return m_metaGuid;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SetCopyGuid(const ibGuid& guid) const { m_metaCopyGuid = guid; }

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	wxString GetFileName() const;
	wxString GetFullName() const;

	wxString GetModuleName() const;
	wxString GetDocPath() const { return m_metaGuid.str(); }

	//filter children element
	virtual bool FilterChild(const ibClassID& clsid) const { return false; }

	//process choice 
	virtual bool ProcessChoice(ibBackendControlFrame* ownerValue,
		const wxString& strFormName, int selMode) {
		return true;
	}

	//methods 
	virtual ibValueMethodHelper* GetPMethods() const override { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const override; // this method is automatically called to initialize attribute and method names.

	//attributes
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;                   //attribute value

	//support icons
	virtual wxIcon GetIcon() const override { return wxNullIcon; }
	static wxIcon GetIconGroup() { return wxNullIcon; }

	//load & save object in metaObject 
	bool LoadMeta(ibReaderMemory& dataReader);
	bool SaveMeta(ibWriterMemory& dataWritter);

	//load & save object
	bool LoadMetaObject(ibMetaData* metaData, ibReaderMemory& dataReader);
	bool SaveMetaObject(ibMetaData* metaData, ibWriterMemory& dataWritter, int flags = defaultFlag);
	bool DeleteMetaObject(ibMetaData* metaData);

	// save & delete object in DB 
	bool CreateMetaTable(ibMetaDataConfiguration* srcMetaData, int flags = createMetaTable);
	bool UpdateMetaTable(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject);
	bool DeleteMetaTable(ibMetaDataConfiguration* srcMetaData);

	// load & save config data 
	virtual bool LoadTableData(const ibReaderMemory& reader) { return true; }
	virtual bool SaveTableData(ibWriterMemory& writer) const { return true; }

	//events: 
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags) { return true; }
	virtual bool OnDeleteMetaObject();
	virtual bool OnRenameMetaObject(const wxString& sNewName) { return true; }

	//for designer 
	virtual bool OnReloadMetaObject() { return true; }

	//module manager is started or exit 
	//after and before for designer 
	virtual bool OnBeforeRunMetaObject(int flags) { return true; }
	virtual bool OnAfterRunMetaObject(int flags) { return true; }

	virtual bool OnBeforeCloseMetaObject() { return true; }
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu) { return false; }
	virtual void ProcessCommand(unsigned int id) {}

	//check is empty
	virtual bool IsEmpty() const override { return false; }

	virtual bool Init() final override;
	virtual bool Init(ibValue** paParams, const long lSizeArray) final override;

	//Is editable object? 
	virtual bool IsEditable() const override;

	//compare object 
	virtual bool CompareObject(const ibValueMetaObject* metaObject) const;

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property) override;
	virtual void OnPropertySelected(ibProperty* property) override;
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	bool ChangeChildPosition(ibValueMetaObject* obj, unsigned int pos);

	//copy & paste object 
	bool CopyObject(ibWriterMemory& writer) const;
	bool PasteObject(ibReaderMemory& reader);

#pragma region __array_h__

	//any
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(
		std::vector<_T1*> array = std::vector<_T1*>()) const {
		FillArrayObjectByFilter<_T1>(array, {});
		return array;
	}

#pragma endregion 
#pragma region __filter_h__

	//any 
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id) const {
		return FindObjectByFilter<_T1>(id, {});
	}

#pragma endregion 

	template<typename T, typename... Args>
	T* CreateMetaObjectAndSetParent(Args&&... args) {
		T* createdObject = ibValue::CreateAndConvertObjectValueRef<T>(args...);
		wxASSERT(createdObject);
		//set child/parent
		createdObject->SetParent(this);
		this->AddChild(createdObject);
		return createdObject;
	}

protected:

	//create and update table 
	virtual bool CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags) { return true; }

	//load & save metaData from DB 
	virtual bool LoadData(ibReaderMemory& reader) { return true; }
	virtual bool SaveData(ibWriterMemory& writer) { return true; }
	virtual bool DeleteData() { return true; }

protected:

#pragma region interface_h
	virtual void DoSetInterface(const ibMetaID& id, const bool& val = true) override;
#pragma endregion

#pragma region role_h
	virtual void DoSetRight(const ibRole* role, const bool& val = true) override;
#pragma endregion

	//Check is full access 
	virtual bool IsFullAccess() const override;

	//Create user info
	virtual ibRoleUserInfo GetUserRoleInfo() const override;

#pragma region __array_h__

	template <typename _T1>
	bool FillArrayObjectByFilter(
		std::vector<_T1*>& array,
		std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		for (auto& child : m_children) {

			if (!child->IsAllowed())
				continue;

			if (filter.size() > 0) {
				bool success = false;
				ibClassID child_clsid = child->GetClassType();
				for (const auto filter_clsid : filter) {
					if (child_clsid == filter_clsid) {
						success = true;
						break;
					}
				}

				if (success)
					array.emplace_back(static_cast<_T1*>(child));
			}
			else {
				_T1* ptr = dynamic_cast<_T1*>(child);
				if (ptr != nullptr) array.emplace_back(ptr);
			}

			if (use_child_filter)
				child->FillArrayObjectByFilter<_T1>(array, filter, true);
		}

		return array.size() > 0;
	}

#pragma endregion 
#pragma region __filter_h__

	template<typename _T1>
	_T1* FindObjectByFilter(const wxString& name,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		if (name.IsEmpty())
			return nullptr;

		for (auto& child : m_children) {

			if (child->IsDeleted())
				continue;

			if (stringUtils::CompareString(name, child->GetName())) {

				if (filter.size() > 0) {

					bool success = false;
					ibClassID child_clsid = child->GetClassType();
					for (const auto filter_clsid : filter) {
						if (child_clsid == filter_clsid) {
							success = true;
							break;
						}
					}

					return success ?
						static_cast<_T1*>(child) : nullptr;
				}

				return dynamic_cast<_T1*>(child);
			}

			if (use_child_filter) {
				_T1* founded = child->FindObjectByFilter<_T1>(name, filter, true);
				if (founded != nullptr)
					return founded;
			}
		}

		//self 
		if (stringUtils::CompareString(name, GetName()))
			return dynamic_cast<_T1*>(const_cast<ibValueMetaObject*>(this));

		return nullptr;
	}

	template<typename _T1>
	_T1* FindObjectByFilter(const ibMetaID& id,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		if (id <= 0)
			return nullptr;

		for (auto& child : m_children) {

			if (child->IsDeleted())
				continue;

			if (child->CompareId(id)) {

				if (filter.size() > 0) {

					bool success = false;
					ibClassID child_clsid = child->GetClassType();
					for (const auto filter_clsid : filter) {
						if (child_clsid == filter_clsid) {
							success = true;
							break;
						}
					}

					return success ?
						static_cast<_T1*>(child) : nullptr;
				}

				return dynamic_cast<_T1*>(child);
			}

			if (use_child_filter) {
				_T1* founded = child->FindObjectByFilter<_T1>(id, filter, true);
				if (founded != nullptr)
					return founded;
			}
		}

		//self 
		if (CompareId(id))
			return dynamic_cast<_T1*>(const_cast<ibValueMetaObject*>(this));

		return nullptr;
	}

	template<typename _T1>
	_T1* FindObjectByFilter(const ibGuid& id,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter = false) const
	{
		if (!id.isValid())
			return nullptr;

		for (auto& child : m_children) {

			if (child->IsDeleted())
				continue;

			if (child->CompareGuid(id)) {

				if (filter.size() > 0) {

					bool success = false;
					ibClassID child_clsid = child->GetClassType();
					for (const auto filter_clsid : filter) {
						if (child_clsid == filter_clsid) {
							success = true;
							break;
						}
					}

					return success ?
						static_cast<_T1*>(child) : nullptr;
				}

				return dynamic_cast<_T1*>(child);
			}

			if (use_child_filter) {
				_T1* founded = child->FindObjectByFilter<_T1>(id, filter, true);
				if (founded != nullptr)
					return founded;
			}
		}

		//self 
		if (CompareGuid(id))
			return dynamic_cast<_T1*>(const_cast<ibValueMetaObject*>(this));

		return nullptr;
	}

#pragma endregion 

protected:

	friend class ibMetaData;

	mutable ibGuid m_metaCopyGuid, m_metaPasteGuid;

	int m_metaFlags;
	ibMetaID m_metaId;			//type id (default is undefined)
	ibGuid m_metaGuid;

	ibMetaData* m_metaData;
	ibValueMethodHelper* m_methodHelper;

	wxString m_strHelpContent;

protected:

	ibPropertyCategory* m_categoryCommon = ibPropertyObject::CreatePropertyCategory(wxT("Common"), _("Common"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_categoryCommon, wxT("Name"), _("Name"), _("Name of metadata object"), wxEmptyString);
	ibPropertyTString* m_propertySynonym = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCommon, wxT("Synonym"), _("Synonym"), _("Synonym of metadata object"), wxEmptyString);
	ibPropertyString* m_propertyComment = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryCommon, wxT("Comment"), _("Comment"), _("Comment"), wxEmptyString);
	ibPropertyCategory* m_categoryContext = ibPropertyObject::CreatePropertyCategory(wxT("Context"), _("Context"));
};

extern BACKEND_API ibRestructureInfo s_restructureInfo;

#endif