#ifndef _METAOBJECT_H__
#define _METAOBJECT_H__

#include "compiler/value.h"
#include "common/propertyObject.h"
#include "utils/fs/fs.h"
#include "guid/guid.h"

class CMetaModuleObject;
class IMetaFormObject;

//reference data
struct reference_t
{
	meta_identifier_t m_id; // id of metadata 
	guid_t m_guid;

	reference_t(const meta_identifier_t& id, const guid_t& guid) : m_id(id), m_guid(guid) {}
};

class IConfigMetadata;

//*******************************************************************************
//*                          define commom clsid                                *
//*******************************************************************************

//COMMON METADATA
const CLASS_ID g_metaCommonMetadataCLSID = TEXT2CLSID("MD_MTD");

//COMMON OBJECTS
const CLASS_ID g_metaCommonModuleCLSID = TEXT2CLSID("MD_CMOD");
const CLASS_ID g_metaCommonFormCLSID = TEXT2CLSID("MD_CFRM");
const CLASS_ID g_metaCommonTemplateCLSID = TEXT2CLSID("MD_CTMP");

const CLASS_ID g_metaRoleCLSID = TEXT2CLSID("MD_ROLE");
const CLASS_ID g_metaInterfaceCLSID = TEXT2CLSID("MD_INTF");

//ADVANCED OBJECTS
const CLASS_ID g_metaAttributeCLSID = TEXT2CLSID("MD_ATTR");
const CLASS_ID g_metaFormCLSID = TEXT2CLSID("MD_FRM");
const CLASS_ID g_metaTemplateCLSID = TEXT2CLSID("MD_TMPL");
const CLASS_ID g_metaModuleCLSID = TEXT2CLSID("MD_MOD");
const CLASS_ID g_metaManagerCLSID = TEXT2CLSID("MD_MNGR");
const CLASS_ID g_metaTableCLSID = TEXT2CLSID("MD_TBL");
const CLASS_ID g_metaEnumCLSID = TEXT2CLSID("MD_ENUM");
const CLASS_ID g_metaDimensionCLSID = TEXT2CLSID("MD_DMNT");
const CLASS_ID g_metaResourceCLSID = TEXT2CLSID("MD_RESS");

const CLASS_ID g_metaFolderAttributeCLSID = TEXT2CLSID("MD_GATR");
const CLASS_ID g_metaFolderTableCLSID = TEXT2CLSID("MD_GTBL");

//SPECIAL OBJECTS
const CLASS_ID g_metaDefaultAttributeCLSID = TEXT2CLSID("MD_DATT");

//MAIN OBJECTS
const CLASS_ID g_metaConstantCLSID = TEXT2CLSID("MD_CONS");
const CLASS_ID g_metaCatalogCLSID = TEXT2CLSID("MD_CAT");
const CLASS_ID g_metaDocumentCLSID = TEXT2CLSID("MD_DOC");
const CLASS_ID g_metaEnumerationCLSID = TEXT2CLSID("MD_ENM");
const CLASS_ID g_metaDataProcessorCLSID = TEXT2CLSID("MD_DPR");
const CLASS_ID g_metaReportCLSID = TEXT2CLSID("MD_RPT");
const CLASS_ID g_metaInformationRegisterCLSID = TEXT2CLSID("MD_INFR");
const CLASS_ID g_metaAccumulationRegisterCLSID = TEXT2CLSID("MD_ACCR");

// EXTERNAL
const CLASS_ID g_metaExternalDataProcessorCLSID = TEXT2CLSID("MD_EDPR");
const CLASS_ID g_metaExternalReportCLSID = TEXT2CLSID("MD_ERPT");

//*******************************************************************************
//*                             IMetaObject                                     *
//*******************************************************************************

#define defaultMetaID 1000

//flags metadata 
#define defaultFlag		  0x0000000

#define onlyLoadFlag	  0x0001000
#define saveConfigFlag	  0x0002000
#define saveToFileFlag	  0x0004000
#define forceCloseFlag	  0x0008000

//flags metaobject 
#define metaDeletedFlag	  0x0001000
#define metaCanSaveFlag	  0x0002000
#define metaNewObjectFlag 0x0004000
#define metaDisableObjectFlag 0x0008000

#define metaDefaultFlag	  metaCanSaveFlag

//flags save
#define createMetaTable  0x0001000
#define updateMetaTable	 0x0002000	
#define deleteMetaTable	 0x0004000	

class Role {
	wxString m_name;
	wxString m_label;
	IMetaObject* m_object; // pointer to the owner object
	bool m_defValue;  // handler function name
	friend class IMetaObject;
private:
	void InitRole(IMetaObject* metaObject, const bool& value = true);
protected:
	Role(const wxString& roleName, const wxString& roleLabel,
		IMetaObject* metaObject, const bool& value = true) :
		m_name(roleName),
		m_label(roleLabel),
		m_object(metaObject),
		m_defValue(value)
	{
		InitRole(metaObject, value);
	}
public:

	bool GetDefValue() const {
		return m_defValue;
	}

	wxString GetName() const {
		return m_name;
	}

	IMetaObject* GetObject() const {
		return m_object;
	}

	wxString GetLabel() const {
		return m_label.IsEmpty() ?
			m_name : m_label;
	}
};

class IMetaObject : public IPropertyObject,
	public CValue {
	wxDECLARE_ABSTRACT_CLASS(IMetaObject);
protected:

	struct roleName_t {
		wxString m_roleName;
		wxString m_roleLabel;
		roleName_t(const wxString& roleName) : m_roleName(roleName) {}
		roleName_t(const wxString& roleName, const wxString& roleLabel) : m_roleName(roleName), m_roleLabel(roleLabel) {}
		roleName_t(const char* propName) : m_roleName(propName) {}
	};

	Role* CreateRole(const roleName_t& name) {
		return new Role(name.m_roleName, name.m_roleLabel, this);
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

	CLASS_ID GetClsid() const { return m_metaClsid; }
	void SetClsid(const CLASS_ID& clsid) { m_metaClsid = clsid; }

	meta_identifier_t GetMetaID() const { return m_metaId; }
	void SetMetaID(const meta_identifier_t& id) { m_metaId = id; }

	wxString GetName() const {
		return m_propertyName->GetValueAsString();
	}

	void SetName(const wxString& name) {
		m_propertyName->SetValue(name);
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

	void SetMetadata(IMetadata* metaData) {
		m_metaData = metaData;
	}

	IMetadata* GetMetadata() const {
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

public:

	bool IsAllowed() const {
		return IsEnabled()
			&& !IsDeleted();
	}

	bool IsEnabled() const {
		return (m_metaFlags & metaDisableObjectFlag) == 0;
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
		auto foundedIt = std::find_if(m_roles.begin(), m_roles.end(), [roleName](const std::pair<wxString, Role*>& pair) {
			return roleName == pair.first;
			}
		);
		if (foundedIt == m_roles.end())
			return false;
		return AccessRight(foundedIt->second, id);
	}

	bool SetRight(const Role* role, const meta_identifier_t& id, const bool& val);
	bool SetRight(const wxString& roleName, const meta_identifier_t& id, const bool& val) {
		if (roleName.IsEmpty())
			return false;
		auto foundedIt = std::find_if(m_roles.begin(), m_roles.end(), [roleName](const std::pair<wxString, Role*>& pair) {
			return roleName == pair.first;
			}
		);

		if (foundedIt == m_roles.end())
			return false;
		return SetRight(foundedIt->second, id, val);
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
		const wxString& name = wxEmptyString,
		const wxString& synonym = wxEmptyString,
		const wxString& comment = wxEmptyString
	);

	virtual ~IMetaObject();

	//system override 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_METADATA;
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("metadata");
	}

	virtual Guid GetGuid() const {
		return m_metaGuid;
	}

	virtual wxString GetFileName() const;
	virtual wxString GetFullName() const;

	virtual wxString GetModuleName() const;
	virtual wxString GetDocPath() const;

	//create in this metaObject 
	bool AppendChild(IMetaObject* child) {
		if (!FilterChild(child->GetClsid()))
			return false;
		m_metaObjects.push_back(child);
		return true;
	}

	void RemoveChild(IMetaObject* child) {
		auto itFounded = std::find(m_metaObjects.begin(), m_metaObjects.end(), child);
		if (itFounded != m_metaObjects.end()) 
			m_metaObjects.erase(itFounded);
	}

	virtual bool FilterChild(const CLASS_ID& clsid) const {
		return false;
	}

	//process choice 
	virtual bool ProcessChoice(IValueFrame* ownerValue, 
		const meta_identifier_t& id, enum eSelectMode selMode) {
		return true;
	}

	virtual bool ProcessListChoice(IValueFrame* ownerValue,
		const meta_identifier_t& id, enum eSelectMode selMode) {
		return true;
	}

	virtual IMetaObject* FindByName(const CLASS_ID& clsid, const wxString& docPath) const {
		for (auto obj : GetObjects(clsid)) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	virtual std::vector<IMetaObject*> GetObjects() const {
		return m_metaObjects;
	}

	virtual std::vector<IMetaObject*> GetObjects(const CLASS_ID& clsid) const {
		std::vector<IMetaObject*> metaObjects;
		for (auto obj : m_metaObjects) {
			if (clsid == obj->GetClsid()) {
				metaObjects.push_back(obj);
			}
		}
		return metaObjects;
	}

	virtual IMetaObject* FindByName(const wxString& docPath) const {
		for (auto obj : m_metaObjects) {
			if (docPath == obj->GetDocPath())
				return obj;
		}
		return NULL;
	}

	//methods 
	virtual CMethods* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		PrepareNames();
		return m_methods;
	}

	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       //вызов метода

	//attributes 
	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута
	virtual int  FindAttribute(const wxString& sName) const;

	virtual wxString GetTypeString() const {
		return GetObjectTypeName() << wxT(".") << GetClassName();
	}

	virtual wxString GetString() const {
		return GetName();
	}

	//support icons
	virtual wxIcon GetIcon() { return wxNullIcon; }
	static wxIcon GetIconGroup() { return wxNullIcon; }

	//load & save object in metaObject 
	bool LoadMeta(CMemoryReader& dataReader);
	bool SaveMeta(CMemoryWriter& dataWritter = CMemoryWriter());

	//load & save object
	bool LoadMetaObject(IMetadata* metaData, CMemoryReader& dataReader);
	bool SaveMetaObject(IMetadata* metaData, CMemoryWriter& dataWritter, int flags = defaultFlag);
	bool DeleteMetaObject(IMetadata* metaData);

	// save & delete object in DB 
	bool CreateMetaTable(IConfigMetadata* srcMetaData);
	bool UpdateMetaTable(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject);
	bool DeleteMetaTable(IConfigMetadata* srcMetaData);

	//events: 
	virtual bool OnCreateMetaObject(IMetadata* metaData);
	virtual bool OnLoadMetaObject(IMetadata* metaData);
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
	IMetaObject* GetParent() const {
		return wxDynamicCast(m_parent, IMetaObject);
	}

	/**
	* Obtiene un hijo del objeto.
	*/
	virtual IMetaObject* GetChild(unsigned int idx) const {
		return dynamic_cast<IMetaObject*>(IPropertyObject::GetChild(idx));
	}

	virtual IMetaObject* GetChild(unsigned int idx, const wxString& type) const {
		return dynamic_cast<IMetaObject*>(IPropertyObject::GetChild(idx, type));
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return false;
	}

	//compare object 
	virtual bool CompareObject(IMetaObject* metaObject) const {
		if (m_metaClsid != metaObject->GetClsid())
			return false;
		if (m_metaId != metaObject->GetMetaID())
			return false;
		for (unsigned int idx = 0; idx < GetPropertyCount(); idx++) {
			Property* propDst = GetProperty(idx);
			wxASSERT(propDst);
			Property* propSrc = metaObject->GetProperty(propDst->GetName());
			if (propSrc == NULL)
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
	virtual void OnPropertyChanged(Property* property);

	/**
	* Devuelve la posicion del hijo o GetChildCount() en caso de no encontrarlo
	*/
	virtual bool ChangeChildPosition(IPropertyObject* obj, unsigned int pos);

private:

	//get metadata
	virtual IMetadata* GetMetaData() const override;

protected:

	//load & save event in control 
	virtual bool LoadRole(CMemoryReader& reader);
	virtual bool SaveRole(CMemoryWriter& writer = CMemoryWriter());

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags) { return true; }

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader& reader) { return true; }
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter()) { return true; }
	virtual bool DeleteData() { return true; }

	bool ReadProperty(CMemoryReader& reader);
	bool SaveProperty(CMemoryWriter& writer) const;

protected:

	int m_metaFlags;

	CLASS_ID m_metaClsid;               //type object name
	meta_identifier_t m_metaId;			//type id (default is undefined)
	Guid m_metaGuid;

	IMetadata* m_metaData;

	std::vector<std::pair<wxString, Role*>> m_roles;
	std::map<meta_identifier_t,
		std::map<wxString, bool>
	> m_valRoles;

	std::vector<IMetaObject*> m_metaObjects;

	CMethods* m_methods;
};

#endif