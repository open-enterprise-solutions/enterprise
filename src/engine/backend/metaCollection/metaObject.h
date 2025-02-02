#ifndef _META_OBJECT_H__
#define _META_OBJECT_H__

#include "backend/compiler/value/value.h"
#include "backend/wrapper/propertyInfo.h"

#include "backend/metaCtor.h"
#include "backend/backend_metatree.h"
#include "backend/role.h"

#include "backend/fileSystem/fs.h"

//*******************************************************************************
class BACKEND_API IMetaData;
//*******************************************************************************
//*                          define commom clsid                                *
//*******************************************************************************

//COMMON METADATA
const class_identifier_t g_metaCommonMetadataCLSID = string_to_clsid("MD_MTD");

//COMMON OBJECTS
const class_identifier_t g_metaCommonModuleCLSID = string_to_clsid("MD_CMOD");
const class_identifier_t g_metaCommonFormCLSID = string_to_clsid("MD_CFRM");
const class_identifier_t g_metaCommonTemplateCLSID = string_to_clsid("MD_CTMP");

const class_identifier_t g_metaRoleCLSID = string_to_clsid("MD_ROLE");
const class_identifier_t g_metaInterfaceCLSID = string_to_clsid("MD_INTF");

//ADVANCED OBJECTS
const class_identifier_t g_metaAttributeCLSID = string_to_clsid("MD_ATTR");
const class_identifier_t g_metaFormCLSID = string_to_clsid("MD_FRM");
const class_identifier_t g_metaTemplateCLSID = string_to_clsid("MD_TMPL");
const class_identifier_t g_metaModuleCLSID = string_to_clsid("MD_MOD");
const class_identifier_t g_metaManagerCLSID = string_to_clsid("MD_MNGR");
const class_identifier_t g_metaTableCLSID = string_to_clsid("MD_TBL");
const class_identifier_t g_metaEnumCLSID = string_to_clsid("MD_ENUM");
const class_identifier_t g_metaDimensionCLSID = string_to_clsid("MD_DMNT");
const class_identifier_t g_metaResourceCLSID = string_to_clsid("MD_RESS");

//SPECIAL OBJECTS
const class_identifier_t g_metaDefaultAttributeCLSID = string_to_clsid("MD_DATT");

//MAIN OBJECTS
const class_identifier_t g_metaConstantCLSID = string_to_clsid("MD_CONS");
const class_identifier_t g_metaCatalogCLSID = string_to_clsid("MD_CAT");
const class_identifier_t g_metaDocumentCLSID = string_to_clsid("MD_DOC");
const class_identifier_t g_metaEnumerationCLSID = string_to_clsid("MD_ENM");
const class_identifier_t g_metaDataProcessorCLSID = string_to_clsid("MD_DPR");
const class_identifier_t g_metaReportCLSID = string_to_clsid("MD_RPT");
const class_identifier_t g_metaInformationRegisterCLSID = string_to_clsid("MD_INFR");
const class_identifier_t g_metaAccumulationRegisterCLSID = string_to_clsid("MD_ACCR");

// EXTERNAL
const class_identifier_t g_metaExternalDataProcessorCLSID = string_to_clsid("MD_EDPR");
const class_identifier_t g_metaExternalReportCLSID = string_to_clsid("MD_ERPT");

//*******************************************************************************
//*                             IMetaObject                                     *
//*******************************************************************************

#define defaultMetaID 1000

//flags metaData 
#define defaultFlag		  0x0000000

#define onlyLoadFlag	  0x0001000
#define saveConfigFlag	  0x0002000
#define saveToFileFlag	  0x0004000
#define forceCloseFlag	  0x0008000
#define forceRunFlag	  0x0016000
#define newObjectFlag	  0x0032000

//flags metaobject 
#define metaDeletedFlag	  0x0001000
#define metaCanSaveFlag	  0x0002000
#define metaDisableFlag	  0x0008000

#define metaDefaultFlag	  metaCanSaveFlag

//flags save
#define createMetaTable  0x0001000
#define updateMetaTable	 0x0002000	
#define deleteMetaTable	 0x0004000	

class BACKEND_API IMetaObject : public CValue,
	public IPropertyObject {
	wxDECLARE_ABSTRACT_CLASS(IMetaObject);
protected:

	struct roleName_t {
		wxString m_roleName;
		wxString m_roleLabel;
		roleName_t(const wxString& roleName) : m_roleName(roleName) {}
		roleName_t(const wxString& roleName, const wxString& roleLabel) : m_roleName(roleName), m_roleLabel(roleLabel) {}
		roleName_t(const char* strPropName) : m_roleName(strPropName) {}
	};

	Role* CreateRole(const roleName_t& strName) {
		return new Role(strName.m_roleName, strName.m_roleLabel, this);
	}

protected:

	PropertyCategory* m_categoryCommon = IPropertyObject::CreatePropertyCategory({ "common", _("common") });

	Property* m_propertyName = IPropertyObject::CreateProperty(m_categoryCommon, { "name" , _("name") }, PropertyType::PT_WXNAME);
	Property* m_propertySynonym = IPropertyObject::CreateProperty(m_categoryCommon, { "synonym", _("synonym") }, PropertyType::PT_WXSTRING);
	Property* m_propertyComment = IPropertyObject::CreateProperty(m_categoryCommon, { "comment", _("comment") }, PropertyType::PT_WXSTRING);

protected:

	std::vector<std::pair<wxString, Role*>>& GetRoles() {
		return m_roles;
	}

	friend class Role;

	/**
	* Añade una propiedad al objeto.
	*
	* Este método será usado por el registro de descriptores para crear la
	* instancia del objeto.
	* Los objetos siempre se crearán a través del registro de descriptores.
	*/
	void AddRole(Role* value);

public:

	meta_identifier_t GetMetaID() const { return m_metaId; }
	void SetMetaID(const meta_identifier_t& id) { m_metaId = id; }

	wxString GetName() const {
		return m_propertyName->GetValueAsString();
	}

	void SetName(const wxString& strName) {
		m_propertyName->SetValue(strName);
	}

	wxString GetSynonym() const {
		return m_propertySynonym->IsOk() ?
			m_propertySynonym->GetValueAsString() : GetName();
	}

	void SetSynonym(const wxString& synonym) {
		m_propertySynonym->SetValue(synonym);
	}

	wxString GetComment() const {
		return m_propertyComment->GetValueAsString();
	}

	void SetComment(const wxString& comment) {
		m_propertyComment->SetValue(comment);
	}

	virtual void SetMetaData(IMetaData* metaData) {
		m_metaData = metaData;
	}

	virtual IMetaData* GetMetaData() const {
		return m_metaData;
	}

	void ResetGuid();
	void ResetId();

	void GenerateGuid() {
		wxASSERT(!m_metaGuid.isValid());
		if (!m_metaGuid.isValid()) {
			ResetGuid();
		}
	}

	operator meta_identifier_t() const {
		return m_metaId;
	}

	IBackendMetadataTree* GetMetaDataTree() const;

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

#pragma region role 

	bool AccessRight(const Role* role) const {
		return AccessRight(role->GetName());
	}
	bool AccessRight(const wxString& roleName) const {
		return true;
	}
	bool AccessRight(const Role* role, const wxString& userName) const {
		return AccessRight(role->GetName(), userName);
	}
	bool AccessRight(const wxString& roleName, const wxString& userName) const {
		return AccessRight(roleName);
	}
	bool AccessRight(const Role* role, const meta_identifier_t& id) const;
	bool AccessRight(const wxString& roleName, const meta_identifier_t& id) const {
		if (roleName.IsEmpty())
			return false;
		auto& it = std::find_if(m_roles.begin(), m_roles.end(), [roleName](const std::pair<wxString, Role*>& pair) {
			return roleName == pair.first;
			}
		);
		if (it == m_roles.end())
			return false;
		return AccessRight(it->second, id);
	}

	bool SetRight(const Role* role, const meta_identifier_t& id, const bool& val);
	bool SetRight(const wxString& roleName, const meta_identifier_t& id, const bool& val) {
		if (roleName.IsEmpty())
			return false;
		auto& it = std::find_if(m_roles.begin(), m_roles.end(), [roleName](const std::pair<wxString, Role*>& pair) {
			return roleName == pair.first;
			}
		);
		if (it == m_roles.end())
			return false;
		return SetRight(it->second, id, val);
	}

	/**
	* Obtiene la propiedad identificada por el nombre.
	*
	* @note Notar que no existe el método SetProperty, ya que la modificación
	*       se hace a través de la referencia.
	*/
	Role* GetRole(const wxString& nameParam) const;

	/**
	* Obtiene el número de propiedades del objeto.
	*/
	unsigned int GetRoleCount() const {
		return (unsigned int)m_roles.size();
	}

	Role* GetRole(unsigned int idx) const; // throws ...;

#pragma endregion

	IMetaObject(
		const wxString& strName = wxEmptyString,
		const wxString& synonym = wxEmptyString,
		const wxString& comment = wxEmptyString
	);

	virtual ~IMetaObject();

	//system override 
	virtual int GetComponentType() const final {
		return COMPONENT_TYPE_METADATA;
	}

	virtual wxString GetClassName() const final {
		return CValue::GetClassName();
	}

	virtual wxString GetObjectTypeName() const final {
		return CValue::GetClassName();
	};

	Guid GetGuid() const {
		return m_metaGuid;
	}

	wxString GetFileName() const;
	wxString GetFullName() const;

	wxString GetModuleName() const;
	wxString GetDocPath() const;

	//create in this metaObject 
	bool AppendChild(IMetaObject* child) {
		if (!FilterChild(child->GetClassType()))
			return false;
		m_listMetaObject.push_back(child);
		return true;
	}

	void RemoveChild(IMetaObject* child) {
		auto it = std::find(m_listMetaObject.begin(), m_listMetaObject.end(), child);
		if (it != m_listMetaObject.end())
			m_listMetaObject.erase(it);
	}

	virtual bool FilterChild(const class_identifier_t& clsid) const {
		return false;
	}

	//process choice 
	virtual bool ProcessChoice(IBackendControlFrame* ownerValue,
		const meta_identifier_t& id, enum eSelectMode selMode) {
		return true;
	}

	std::vector<IMetaObject*> GetObjects() const {
		return m_listMetaObject;
	}

	std::vector<IMetaObject*> GetObjects(const class_identifier_t& clsid) const {
		std::vector<IMetaObject*> metaObjects;
		for (auto& obj : m_listMetaObject) {
			if (clsid == obj->GetClassType()) {
				metaObjects.push_back(obj);
			}
		}
		return metaObjects;
	}

	IMetaObject* FindByName(const wxString& strDocPath) const;
	IMetaObject* FindByName(const class_identifier_t& clsid, const wxString& strDocPath) const;

	//methods 
	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames();
		return m_methodHelper;
	}

	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	//attributes 
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	//support icons
	virtual wxIcon GetIcon() const { return wxNullIcon; }
	static wxIcon GetIconGroup() { return wxNullIcon; }

	//load & save object in metaObject 
	bool LoadMeta(CMemoryReader& dataReader);
	bool SaveMeta(CMemoryWriter& dataWritter = CMemoryWriter());

	//load & save object
	bool LoadMetaObject(IMetaData* metaData, CMemoryReader& dataReader);
	bool SaveMetaObject(IMetaData* metaData, CMemoryWriter& dataWritter, int flags = defaultFlag);
	bool DeleteMetaObject(IMetaData* metaData);

	// save & delete object in DB 
	bool CreateMetaTable(IMetaDataConfiguration* srcMetaData);
	bool UpdateMetaTable(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject);
	bool DeleteMetaTable(IMetaDataConfiguration* srcMetaData);

	//events: 
	virtual bool OnCreateMetaObject(IMetaData* metaData);
	virtual bool OnLoadMetaObject(IMetaData* metaData);
	virtual bool OnSaveMetaObject() { return true; }
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

	// Gets the parent object
	template <typename retType = IMetaObject >
	inline retType* GetParent() const {
		return wxDynamicCast(m_parent, retType);
	}

	/**
	* Obtiene un hijo del objeto.
	*/
	IMetaObject* GetChild(unsigned int idx) const {
		return wxDynamicCast(IPropertyObject::GetChild(idx), IMetaObject);
	}

	IMetaObject* GetChild(unsigned int idx, const wxString& type) const {
		return wxDynamicCast(IPropertyObject::GetChild(idx, type), IMetaObject);
	}

	//check is empty
	virtual inline bool IsEmpty() const {
		return false;
	}

	virtual bool Init() final override;
	virtual bool Init(CValue** paParams, const long lSizeArray) final override;

	//compare object 
	virtual bool CompareObject(IMetaObject* metaObject) const {
		if (GetClassType() != metaObject->GetClassType())
			return false;
		if (m_metaId != metaObject->GetMetaID())
			return false;
		for (unsigned int idx = 0; idx < GetPropertyCount(); idx++) {
			Property* propDst = GetProperty(idx);
			wxASSERT(propDst);
			Property* propSrc = metaObject->GetProperty(propDst->GetName());
			if (propSrc == nullptr)
				return false;
			if (propDst->GetValue() != propSrc->GetValue())
				return false;
		}
		return true;
	}

	//copy & paste object 
	virtual bool CopyObject(CMemoryWriter& writer) const;
	virtual bool PasteObject(CMemoryReader& reader);

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertySelected(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	virtual bool ChangeChildPosition(IPropertyObject* obj, unsigned int pos);

protected:

	//load & save event in control 
	virtual bool LoadRole(CMemoryReader& reader);
	virtual bool SaveRole(CMemoryWriter& writer = CMemoryWriter());

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags) { return true; }

	//load & save metaData from DB 
	virtual bool LoadData(CMemoryReader& reader) { return true; }
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter()) { return true; }
	virtual bool DeleteData() { return true; }

	bool ReadProperty(CMemoryReader& reader);
	bool SaveProperty(CMemoryWriter& writer) const;

protected:

	template<typename retType, typename convType = retType>
	class CMetaVector {
		std::vector<retType*> m_listObject;
	public:
		//////////////////////////////////////////////////////////////////////////
		CMetaVector(const IMetaObject* metaItem, const class_identifier_t& clsid) {
			m_listObject.reserve(metaItem->m_listMetaObject.size());
			std::transform(
				metaItem->m_listMetaObject.begin(),
				metaItem->m_listMetaObject.end(),
				std::back_inserter(m_listObject),
				[](auto const& obj) {
					return dynamic_cast<convType*>(obj);
				}
			);
			m_listObject.erase(
				std::remove_if(m_listObject.begin(), m_listObject.end(), [clsid](auto const& obj) {
					if (obj != nullptr) return clsid != obj->GetClassType(); return obj == nullptr; }), m_listObject.end()
						);
		}
		CMetaVector(const std::vector<IMetaObject*>& listObject) {
			m_listObject.reserve(listObject.size());
			std::transform(
				std::begin(listObject),
				std::end(listObject),
				std::back_inserter(m_listObject),
				[](auto const& obj) {
					return dynamic_cast<convType*>(obj);
				}
			);
			m_listObject.erase(
				std::remove_if(m_listObject.begin(), m_listObject.end(), [](auto const& obj) { return obj == nullptr; }), m_listObject.end()
			);
		}
		//////////////////////////////////////////////////////////////////////////
		CMetaVector(const CMetaVector& src) :
			m_listObject(std::move(src.m_listObject)) {
		}
		//////////////////////////////////////////////////////////////////////////
		auto begin() { return m_listObject.begin(); }
		auto end() { return m_listObject.end(); }
		auto cbegin() const { return m_listObject.cbegin(); }
		auto cend() const { returnm_listObject.cend(); }
		//////////////////////////////////////////////////////////////////////////
		std::vector<retType*> GetVector() {
			return m_listObject;
		}
		//////////////////////////////////////////////////////////////////////////
		operator std::vector<retType*>() {
			return m_listObject;
		}
	};

protected:

	int m_metaFlags;
	meta_identifier_t m_metaId;			//type id (default is undefined)
	Guid m_metaGuid;

	IMetaData* m_metaData;

	std::vector<std::pair<wxString, Role*>> m_roles;
	std::map<meta_identifier_t,
		std::map<wxString, bool>
	> m_valRoles;

	std::vector<IMetaObject*> m_listMetaObject;

	CMethodHelper* m_methodHelper;
};
#endif