#ifndef _METADATA_H__
#define _METADATA_H__

#include "compiler/compiler.h"
#include "metadata/moduleManager/moduleManager.h"
#include "metadata/metaObjects/metaObjectMetadata.h"

class CDocument;

#define metadata             (IConfigMetadata::Get())
#define metadataCreate(mode) (IConfigMetadata::Initialize(mode))
#define metadataDestroy()  	 (IConfigMetadata::Destroy())

class IMetaTypeObjectValueSingle;

class IMetadataWrapperTree {
public:

	virtual void SetReadOnly(bool readOnly = true) = 0;
	virtual void Modify(bool modify) = 0;

	virtual void UpdateChoiceSelection() {}

	virtual void EditModule(const wxString& fullName, int lineNumber, bool setRunLine = true) = 0;

	virtual bool OpenFormMDI(IMetaObject* obj) = 0;
	virtual bool OpenFormMDI(IMetaObject* obj, CDocument*& foundedDoc) = 0;

	virtual bool CloseFormMDI(IMetaObject* obj) = 0;

	virtual CDocument* GetDocument(IMetaObject* obj) const = 0;

	virtual void CloseMetaObject(IMetaObject* obj) = 0;
	virtual void OnCloseDocument(CDocument* doc) = 0;

protected:

	class ITreeClsidData {
		CLASS_ID m_clsid; //тип элемента
	public:
		ITreeClsidData(const CLASS_ID& clsid) : m_clsid(clsid) {}

		CLASS_ID GetClassID() const {
			return m_clsid;
		}
	};

	class ITreeMetaData {
		IMetaObject* m_metaObject; //тип элемента
	public:
		ITreeMetaData(IMetaObject* metaObject) : m_metaObject(metaObject) {}

		void SetMetaObject(IMetaObject* metaObject) {
			m_metaObject = metaObject;
		}

		IMetaObject* GetMetaObject() const {
			return m_metaObject;
		}
	};
};

#define wxOES_Data wxT("OES_Data")

///////////////////////////////////////////////////////////////////////////////

class IMetadata {
	IMetadataWrapperTree* m_metaTree;
private:
	void DoGenerateNewID(meta_identifier_t& id, IMetaObject* top) const;
	//Get metaobjects 
	void DoGetMetaObjects(const CLASS_ID& clsid, std::vector<IMetaObject*>& metaObjects, IMetaObject* top) const;
	//find object
	IMetaObject* DoFindByName(const wxString& fullName, IMetaObject* top) const;
	//get metaObject 
	IMetaObject* DoGetMetaObject(const meta_identifier_t& id, IMetaObject* top) const;
	IMetaObject* DoGetMetaObject(const Guid& guid, IMetaObject* top) const;
public:
	IMetadata(bool readOnly = false) : m_moduleManager(NULL), m_metaTree(NULL), m_metaReadOnly(readOnly), m_metaModify(false) {}
	virtual ~IMetadata() {}

	virtual IModuleManager* GetModuleManager() const {
		return m_moduleManager;
	}

	virtual bool IsModified() const {
		return m_metaModify;
	}

	virtual void Modify(bool modify = true) {
		if (m_metaTree != NULL)
			m_metaTree->Modify(modify);
		m_metaModify = modify;
	}

	virtual void SetVersion(const version_identifier_t& version) = 0;
	virtual version_identifier_t GetVersion() const = 0;

	virtual wxString GetFileName() const {
		return wxEmptyString;
	}
	
	virtual IMetaObject* GetCommonMetaObject() const { 
		return NULL; 
	}

	//runtime support:
	CValue CreateObject(const wxString& className, CValue** aParams = NULL) {
		return CreateObjectRef(className, aParams);
	}

	virtual CValue* CreateObjectRef(const wxString& className, CValue** aParams = NULL);

	template<class retType = CValue>
	retType* CreateAndConvertObjectRef(const wxString& className, CValue** aParams = NULL) {
		return value_cast<retType>(CreateObjectRef(className, aParams));
	}

	void RegisterObject(const wxString& className, IMetaTypeObjectValueSingle* singleObject);
	void UnRegisterObject(const wxString& className);

	virtual bool IsRegisterObject(const wxString& className) const;
	virtual bool IsRegisterObject(const wxString& className, eObjectType objectType) const;
	virtual bool IsRegisterObject(const wxString& className, eObjectType objectType, enum eMetaObjectType refType) const;

	virtual bool IsRegisterObject(const CLASS_ID& clsid) const;

	virtual CLASS_ID GetIDObjectFromString(const wxString& clsName) const;
	virtual wxString GetNameObjectFromID(const CLASS_ID& clsid, bool upper = false) const;

	virtual meta_identifier_t GetVTByID(const CLASS_ID& clsid) const;
	virtual CLASS_ID GetIDByVT(const meta_identifier_t& valueType, enum eMetaObjectType refType) const;

	virtual IMetaTypeObjectValueSingle* GetTypeObject(const CLASS_ID& clsid) const;
	virtual IMetaTypeObjectValueSingle* GetTypeObject(const IMetaObject* metaValue, enum eMetaObjectType refType) const;

	virtual wxArrayString GetAvailableObjects(enum eMetaObjectType refType) const;

	virtual IObjectValueAbstract* GetAvailableObject(const CLASS_ID& clsid) const;
	virtual IObjectValueAbstract* GetAvailableObject(const wxString& className) const;

	virtual std::vector<IMetaTypeObjectValueSingle*> GetAvailableSingleObjects() const;
	virtual std::vector<IMetaTypeObjectValueSingle*> GetAvailableSingleObjects(const CLASS_ID& clsid, eMetaObjectType refType) const;
	virtual std::vector<IMetaTypeObjectValueSingle*> GetAvailableSingleObjects(enum eMetaObjectType refType) const;

	//run/close 
	virtual bool RunMetadata(int flags = defaultFlag) = 0;
	virtual bool CloseMetadata(int flags = defaultFlag) = 0;

	//metaobject
	IMetaObject* CreateMetaObject(const CLASS_ID& clsid, IMetaObject* parentMetaObj);

	bool RenameMetaObject(IMetaObject* obj, const wxString& sNewName);
	void RemoveMetaObject(IMetaObject* obj, IMetaObject* objParent = NULL);

	//Get metaobjects 
	virtual std::vector<IMetaObject*> GetMetaObjects(const CLASS_ID& clsid) const;
	
	//find object
	virtual IMetaObject* FindByName(const wxString& fullName) const;
	
	//get metaObject 
	template <typename retType>
	inline bool GetMetaObject(retType*& foundedVal, const meta_identifier_t& id, IMetaObject* top = NULL) const {
		foundedVal = dynamic_cast<retType *>(GetMetaObject(id, top));
		return foundedVal != NULL; 
	}

	template <typename retType>
	inline bool GetMetaObject(retType*& foundedVal, const Guid& guid, IMetaObject* top = NULL) const {
		foundedVal = dynamic_cast<retType *>(GetMetaObject(guid, top));
		return foundedVal != NULL;
	}

	//get metaObject 
	virtual IMetaObject* GetMetaObject(const meta_identifier_t& id, IMetaObject* top = NULL) const;
	virtual IMetaObject* GetMetaObject(const Guid& guid, IMetaObject* top = NULL) const;

	//Associate this metadata with 
	virtual IMetadataWrapperTree* GetMetaTree() const {
		return m_metaTree;
	}

	virtual void SetMetaTree(IMetadataWrapperTree* metaTree) {
		m_metaTree = metaTree;
	}

	//ID's 
	virtual meta_identifier_t GenerateNewID() const;

	//Generate new name
	virtual wxString GetNewName(const CLASS_ID& clsid, IMetaObject* metaParent, const wxString& sPrefix = wxEmptyString, bool forConstructor = false);

protected:

	bool m_metaModify;
	bool m_metaReadOnly;

	enum
	{
		eHeaderBlock = 0x2320,
		eDataBlock = 0x2350,
		eChildBlock = 0x2370
	};

	//custom types
	std::vector<IMetaTypeObjectValueSingle*> m_aFactoryMetaObjects;

	//common module manager
	IModuleManager* m_moduleManager;
};

class IConfigMetadata : public IMetadata {
	static IConfigMetadata* s_instance;
public:

	IConfigMetadata(bool readOnly) : IMetadata(readOnly) {}

	virtual bool IsConfigSave() const { 
		return true; 
	}

	virtual Guid GetMetadataGuid() const = 0;
	virtual wxString GetMetadataMD5() const = 0;

	virtual wxString GetMetadataName() const = 0;
	virtual wxString GetConfigPath() const = 0;
	virtual wxString GetDefaultSource() const = 0;

	virtual bool CreateMetadata();
	virtual bool LoadMetadata(int flags = defaultFlag) { return true; }
	virtual bool SaveMetadata(int flags = defaultFlag) { return true; }

	//get tablename 
	static wxString GetConfigSaveTableName();
	static wxString GetConfigTableName();
	static wxString GetCompileDataTableName();
	static wxString GetUsersTableName();
	static wxString GetActiveUsersTableName();
	static wxString GetConfigParamsTableName();

	//rollback to config db
	virtual bool RoolbackToConfigDatabase() { return true; }

	//load/save form file
	virtual bool LoadFromFile(const wxString& fileName) { return true; }
	virtual bool SaveToFile(const wxString& fileName) { return true; }

	// get config metadata 
	virtual IConfigMetadata* GetConfigMetadata() const { return NULL; }

public:
	static IConfigMetadata* Get();

	static bool Initialize(enum eRunMode mode);
	static void Destroy();
};

class CConfigFileMetadata : public IConfigMetadata {
public:

	bool ConfigOpened() const {
		return m_configOpened;
	}

	CConfigFileMetadata(bool readOnly = false);
	virtual ~CConfigFileMetadata();

	virtual Guid GetMetadataGuid() const { 
		return m_commonObject->GetDocPath(); 
	}
	
	virtual wxString GetMetadataMD5() const {
		return m_md5Hash; 
	}

	virtual wxString GetMetadataName() const { 
		return m_commonObject->GetName(); 
	}
	
	virtual wxString GetConfigPath() const { 
		return wxEmptyString;
	}
	
	virtual wxString GetDefaultSource() const { 
		return wxEmptyString;
	}

	virtual void SetVersion(const version_identifier_t& version) {
		m_commonObject->SetVersion(version);
	}

	version_identifier_t GetVersion() const {
		return m_commonObject->GetVersion();
	}

	//compare metadata
	virtual bool CompareMetadata(CConfigFileMetadata* dst) const {
		return m_md5Hash == dst->m_md5Hash;
	}

	//run/close 
	virtual bool RunMetadata(int flags = defaultFlag);
	virtual bool CloseMetadata(int flags = defaultFlag);

	virtual bool ClearMetadata();

	//load/save form file
	virtual bool LoadFromFile(const wxString& fileName);

	virtual IMetaObject* GetCommonMetaObject() const {
		return m_commonObject;
	}

protected:

	//header loader/saver 
	bool LoadHeader(CMemoryReader& readerData);

	//loader/saver/deleter: 
	bool LoadCommonMetadata(const CLASS_ID& clsid, CMemoryReader& readerData);
	bool LoadMetadata(const CLASS_ID& clsid, CMemoryReader& readerData, IMetaObject* parentObj);
	bool LoadChildMetadata(const CLASS_ID& clsid, CMemoryReader& readerData, IMetaObject* parentObj);

	//run/close recursively:
	bool RunChildMetadata(IMetaObject* parentObj, int flags, bool before);
	bool CloseChildMetadata(IMetaObject* parentObj, int flags, bool before);
	//clear recursively:
	bool ClearChildMetadata(IMetaObject* parentObj);

protected:

	bool m_configOpened;
	wxString m_md5Hash;
	//common meta object
	CMetaObject* m_commonObject;
};

class CConfigMetadata : public CConfigFileMetadata {
public:
	CConfigMetadata(bool readOnly = false);
	virtual bool LoadFromFile(const wxString& fileName) {
		if (CConfigFileMetadata::LoadFromFile(fileName)) {
			Modify(true); //set modify for check metadata
			return true;
		}
		return false;
	}

	virtual Guid GetMetadataGuid() const {
		return m_metaGuid; 
	}

	virtual wxString GetMetadataName() const { 
		return m_commonObject->GetName(); 
	}
	
	virtual wxString GetConfigPath() const {
		return m_sConfigPath; 
	}
	
	virtual wxString GetDefaultSource() const { 
		return m_sDefaultSource; 
	}

	//metadata 
	virtual bool LoadMetadata(int flags = defaultFlag);

protected:
	Guid m_metaGuid;

	wxString m_sConfigPath;
	wxString m_sDefaultSource;
};

class CConfigStorageMetadata : public CConfigMetadata {
	bool m_configSave;
	CConfigMetadata* m_metaConfig;
public:

	CConfigStorageMetadata(bool readOnly = false);
	virtual ~CConfigStorageMetadata();

	//is config save
	virtual bool IsConfigSave() const {
		return m_configSave;
	}

	//metadata 
	virtual bool CreateMetadata();

	virtual bool LoadMetadata(int flags = defaultFlag) {
		if (!CreateMetadata()) {
			return false;
		}
		if (m_metaConfig->LoadMetadata(onlyLoadFlag)) {
			if (CConfigMetadata::LoadMetadata()) {
				m_configSave =
					CompareMetadata(m_metaConfig);
				Modify(!m_configSave);
				return true;
			}
		}
		return false;
	}

	virtual bool SaveMetadata(int flags = defaultFlag);

	//run/close 
	virtual bool RunMetadata(int flags = defaultFlag) {
		return CConfigMetadata::RunMetadata(flags);
	}

	virtual bool CloseMetadata(int flags = defaultFlag) {

		if (!CConfigMetadata::CloseMetadata(flags))
			return false;

		return m_metaConfig->CloseMetadata(flags);
	}

	//rollback to config db
	virtual bool RoolbackToConfigDatabase();

	//save form file
	virtual bool SaveToFile(const wxString& fileName);

	// get config metadata 
	virtual IConfigMetadata* GetConfigMetadata() const {
		return m_metaConfig;
	}

protected:

	//header saver 
	bool SaveHeader(CMemoryWriter& writterData);

	//loader/saver/deleter: 
	bool SaveCommonMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, int flags = defaultFlag);
	bool SaveMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, int flags = defaultFlag);
	bool SaveChildMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, IMetaObject* parentObj, int flags = defaultFlag);
	bool DeleteCommonMetadata(const CLASS_ID& clsid);
	bool DeleteMetadata(const CLASS_ID& clsid);
	bool DeleteChildMetadata(const CLASS_ID& clsid, IMetaObject* parentObj);
};

#define sign_metadata 0x1236F362122FE

#endif 