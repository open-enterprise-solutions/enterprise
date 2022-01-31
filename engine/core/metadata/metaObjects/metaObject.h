#ifndef _METAOBJECT_H__
#define _METAOBJECT_H__

#include "compiler/value.h"
#include "common/objectbase.h"
#include "utils/fs/fs.h"
#include "guid/guid.h"

class CMetaModuleObject;

//*******************************************************************************
//*                          define commom clsid                                *
//*******************************************************************************

//COMMON METADATA
const CLASS_ID g_metaCommonMetadataCLSID = TEXT2CLSID("MD_MTD");

//COMMON OBJECTS
const CLASS_ID g_metaCommonModuleCLSID = TEXT2CLSID("MD_CMOD");
const CLASS_ID g_metaCommonFormCLSID = TEXT2CLSID("MD_CFRM");
const CLASS_ID g_metaCommonTemplateCLSID = TEXT2CLSID("MD_CTMP");

//ADVANCED OBJECTS
const CLASS_ID g_metaAttributeCLSID = TEXT2CLSID("MD_ATTR");
const CLASS_ID g_metaFormCLSID = TEXT2CLSID("MD_FRM");
const CLASS_ID g_metaTemplateCLSID = TEXT2CLSID("MD_TMPL");
const CLASS_ID g_metaModuleCLSID = TEXT2CLSID("MD_MOD");
const CLASS_ID g_metaManagerCLSID = TEXT2CLSID("MD_MNGR");
const CLASS_ID g_metaTableCLSID = TEXT2CLSID("MD_TBL");
const CLASS_ID g_metaEnumCLSID = TEXT2CLSID("MD_ENUM");

//SPECIAL OBJECTS
const CLASS_ID g_metaDefaultAttributeCLSID = TEXT2CLSID("MD_DATT");

//MAIN OBJECTS
const CLASS_ID g_metaConstantCLSID = TEXT2CLSID("MD_CONS");
const CLASS_ID g_metaCatalogCLSID = TEXT2CLSID("MD_CAT");
const CLASS_ID g_metaDocumentCLSID = TEXT2CLSID("MD_DOC");
const CLASS_ID g_metaEnumerationCLSID = TEXT2CLSID("MD_ENM");
const CLASS_ID g_metaDataProcessorCLSID = TEXT2CLSID("MD_DPR");
const CLASS_ID g_metaReportCLSID = TEXT2CLSID("MD_RPT");

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

//flags metaobject 
#define metaDeletedFlag	  0x0001000
#define metaCanSaveFlag	  0x0002000
#define metaNewObjectFlag 0x0004000

#define metaDefaultFlag	  metaCanSaveFlag

//flags save
#define createMetaTable  0x0001000
#define updateMetaTable	 0x0002000	
#define deleteMetaTable	 0x0004000	

class IMetaObject : public IObjectBase,
	public CValue
{
	wxDECLARE_ABSTRACT_CLASS(IMetaObject);

public:

	CLASS_ID GetClsid() const { return m_metaClsid; }
	void SetClsid(CLASS_ID clsid) { m_metaClsid = clsid; }

	meta_identifier_t GetMetaID() const { return m_metaId; }
	void SetMetaID(meta_identifier_t id) { m_metaId = id; }

	wxString GetName() const { return m_metaName; }
	void SetName(const wxString &name) { m_metaName = name; }

	wxString GetSynonym() const { return (m_metaSynonym.Length() > 0) ? m_metaSynonym : m_metaName; }
	void SetSynonym(const wxString &synonym) { m_metaSynonym = synonym; }

	wxString GetComment() const { return m_metaComment; }
	void SetComment(const wxString &comment) { m_metaComment = comment; }

	void SetMetadata(IMetadata *metaData) { m_metaData = metaData; }
	IMetadata *GetMetadata() const { return m_metaData; }

	void GenerateGuid() { wxASSERT(!m_metaGuid.isValid()); m_metaGuid = Guid::newGuid(); }

public:

	bool IsDeleted() const { return (m_metaFlags & metaDeletedFlag) != 0; }
	void MarkAsDeleted() { m_metaFlags |= metaDeletedFlag; }

public:

	IMetaObject(const wxString &name = wxEmptyString, const wxString &synonym = wxEmptyString, const wxString &comment = wxEmptyString);
	virtual ~IMetaObject();

	//if object have composite collection
	virtual bool IsRefObject() const { return false; }

	//system override 
	virtual int GetComponentType() override { return COMPONENT_TYPE_METADATA; }
	virtual wxString GetObjectTypeName() const override { return wxT("metadata"); }

	virtual wxString GetFileName();
	virtual wxString GetFullName();

	virtual wxString GetModuleName();
	virtual wxString GetDocPath();

	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	virtual void AppendChild(IMetaObject *childObj) {}
	virtual void RemoveChild(IMetaObject *childObj) {}

	virtual bool ProcessChoice(IValueFrame *ownerValue, meta_identifier_t id = wxNOT_FOUND) { return true; }
	virtual bool ProcessListChoice(IValueFrame *ownerValue, meta_identifier_t id = wxNOT_FOUND) { return true; }

	virtual IMetaObject *FindByName(const CLASS_ID &clsid, const wxString &docPath) const 
	{
		for (auto obj : GetObjects(clsid)) {
			if (docPath == obj->GetDocPath())
				return obj;
		}

		return NULL;
	}

	virtual std::vector<IMetaObject *> GetObjects(const CLASS_ID &clsid) const
	{
		std::vector<IMetaObject *> metaObjects;

		for (auto obj : GetObjects()) {
			if (clsid == obj->GetClsid()) {
				metaObjects.push_back(obj);
			}
		}

		return metaObjects;
	}

	virtual IMetaObject *FindByName(const wxString &docPath) const 
	{
		for (auto obj : GetObjects()) {
			if (docPath == obj->GetDocPath())
				return obj;
		}

		return NULL;
	}

	virtual std::vector<IMetaObject *> GetObjects() const { return std::vector<IMetaObject *>(); }

	//methods 
	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);       //вызов метода

	//attributes 
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);                   //значение атрибута
	virtual int  FindAttribute(const wxString &sName) const;

	virtual wxString GetTypeString() const { return GetObjectTypeName() << wxT(".") << GetClassName(); }
	virtual wxString GetString() const { return m_metaName; }

	//support icons
	virtual wxIcon GetIcon() { return wxIcon(); }
	static wxIcon GetIconGroup() { return wxIcon(); }

	//load & save object in metaObject 
	bool LoadMeta(CMemoryReader &dataReader);
	bool SaveMeta(CMemoryWriter &dataWritter = CMemoryWriter());

	//load & save object
	bool LoadMetaObject(IMetadata *metaData, CMemoryReader &dataReader);
	bool SaveMetaObject(IMetadata *metaData, CMemoryWriter &dataWritter, int flags = defaultFlag);
	bool DeleteMetaObject(IMetadata *metaData);

	// save & delete object in DB 
	bool CreateMetaTable(IConfigMetadata *srcMetaData);
	bool UpdateMetaTable(IConfigMetadata *srcMetaData, IMetaObject *srcMetaObject);
	bool DeleteMetaTable(IConfigMetadata *srcMetaData);

	//events: 
	virtual bool OnCreateMetaObject(IMetadata *metaData);
	virtual bool OnLoadMetaObject(IMetadata *metaData);
	virtual bool OnSaveMetaObject() { return true; }
	virtual bool OnDeleteMetaObject();
	virtual bool OnRenameMetaObject(const wxString &sNewName) { return true; }

	//for designer 
	virtual bool OnReloadMetaObject() { return true; }

	//module manager is started or exit 
	virtual bool OnRunMetaObject(int flags) { return true; }
	virtual bool OnCloseMetaObject() { return true; }

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu *defultMenu) { return false; }
	virtual void ProcessCommand(unsigned int id) {}

	// Gets the parent object
	IMetaObject* GetParent() const { return wxDynamicCast(m_parent, IMetaObject); }

	/**
	* Obtiene un hijo del objeto.
	*/
	virtual IMetaObject* GetChild(unsigned int idx) { return dynamic_cast<IMetaObject *>(IObjectBase::GetChild(idx)); }
	virtual IMetaObject* GetChild(unsigned int idx, const wxString& type) { return dynamic_cast<IMetaObject *>(IObjectBase::GetChild(idx, type)); }

	//get module object in compose object 
	virtual CMetaModuleObject *GetModuleObject() { return NULL; }

	//check is empty
	virtual inline bool IsEmpty() const override { return false; }

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property *property);
	virtual void OnPropertySelected(Property *property);
	virtual void OnPropertyChanged(Property *property);

	/**
	* Set property data
	*/
	virtual void SetPropertyData(Property *property, const CValue &srcValue);

	/**
	* Get property data
	*/
	virtual CValue GetPropertyData(Property *property);

private:

	//get metadata
	virtual IMetadata *GetMetaData() const override;

	//get typelist 
	virtual OptionList *GetTypelist() const override;

protected:

	//create and update table 
	virtual bool CreateAndUpdateTableDB(IConfigMetadata *srcMetaData, IMetaObject *srcMetaObject, int flags) { return true; }

	//load & save metadata from DB 
	virtual bool LoadData(CMemoryReader &reader) { return true; }
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter()) { return true; }
	virtual bool DeleteData() { return true; }

protected:

	int m_metaFlags;

	wxString m_metaName;				//имя объекта (идентификатор) 
	wxString m_metaSynonym;				//синоним объекта
	wxString m_metaComment;				//комментарий объекта

	CLASS_ID m_metaClsid;               //type object name
	meta_identifier_t m_metaId;			//type id (default is undefined)
	Guid m_metaGuid;

	IMetadata *m_metaData;
	CMethods *m_methods;
};

#endif