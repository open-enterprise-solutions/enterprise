#include "metadata/metadata.h"
#include "metadata/metaObjects/objects/dataProcessor.h"

#define sign_dataProcessor 0x1345F6621261E

class CMetadataDataProcessor : public IMetadata
{
	wxString m_fullPath;

	IMetadata* m_ownerMeta; //owner for saving/loading
	CMetaObjectDataProcessor* m_commonObject; 	//common meta object

	version_identifier_t m_version;

public:

	CMetadataDataProcessor();
	CMetadataDataProcessor(IMetadata *metaData, CMetaObjectDataProcessor *srcDataProcessor = NULL);
	virtual ~CMetadataDataProcessor();

	virtual CMetaObjectDataProcessor* GetDataProcessor() const { return m_commonObject; }
	virtual IModuleManager* GetModuleManager() const { return m_moduleManager; }

	virtual void SetVersion(const version_identifier_t &version) { m_version = version; }
	virtual version_identifier_t GetVersion() const { return m_version; }

	virtual wxString GetFileName() const {
		return m_fullPath; 
	}

	//runtime support:
	virtual CValue* CreateObjectRef(const wxString& className, CValue** aParams = NULL);

	virtual bool IsRegisterObject(const wxString& className) const;
	virtual bool IsRegisterObject(const wxString& className, eObjectType objectType) const;
	virtual bool IsRegisterObject(const wxString& className, eObjectType objectType, enum eMetaObjectType refType) const;

	virtual bool IsRegisterObject(const CLASS_ID& clsid) const;

	virtual CLASS_ID GetIDObjectFromString(const wxString& clsName) const;
	virtual wxString GetNameObjectFromID(const CLASS_ID& clsid, bool upper = false) const;

	virtual IMetaTypeObjectValueSingle* GetTypeObject(const CLASS_ID& clsid) const;
	virtual IMetaTypeObjectValueSingle* GetTypeObject(const IMetaObject* metaValue, enum eMetaObjectType refType) const;
	
	virtual wxArrayString GetAvailableObjects(enum eMetaObjectType refType) const;

	virtual IObjectValueAbstract* GetAvailableObject(const CLASS_ID& clsid) const;
	virtual IObjectValueAbstract* GetAvailableObject(const wxString& className) const;

	virtual std::vector<IMetaTypeObjectValueSingle*> GetAvailableSingleObjects() const;
	virtual std::vector<IMetaTypeObjectValueSingle*> GetAvailableSingleObjects(const CLASS_ID& clsid, eMetaObjectType refType) const;
	virtual std::vector<IMetaTypeObjectValueSingle*> GetAvailableSingleObjects(enum eMetaObjectType refType) const;

	//metadata 
	virtual bool CreateMetadata();
	virtual bool LoadMetadata();
	virtual bool SaveMetadata();
	virtual bool ClearMetadata();

	//run/close 
	virtual bool RunMetadata(int flags = defaultFlag);
	virtual bool CloseMetadata(int flags = defaultFlag);

	//load/save form file
	bool LoadFromFile(const wxString& fileName);
	bool SaveToFile(const wxString& fileName);

	virtual IMetaObject* GetCommonMetaObject() const { 
		return m_commonObject; 
	}

	//get metaObject 
	virtual IMetaObject* GetMetaObject(const meta_identifier_t &id);

protected:

	//header loader/saver 
	bool LoadHeader(CMemoryReader& readerData);
	bool SaveHeader(CMemoryWriter& writterData);

	//loader/saver/deleter: 
	bool LoadCommonMetadata(const CLASS_ID& clsid, CMemoryReader& readerData);
	bool LoadChildMetadata(const CLASS_ID& clsid, CMemoryReader& readerData, IMetaObject* parentObj);
	bool SaveCommonMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, bool saveToFile = false);
	bool SaveChildMetadata(const CLASS_ID& clsid, CMemoryWriter& writterData, IMetaObject* parentObj, bool saveToFile);
	bool DeleteCommonMetadata(const CLASS_ID& clsid);
	bool DeleteChildMetadata(const CLASS_ID& clsid, IMetaObject* parentObj);

	//run/close recursively:
	bool RunChildMetadata(IMetaObject* parentObj, int flags, bool before);
	bool CloseChildMetadata(IMetaObject* parentObj, int flags, bool before);
	bool ClearChildMetadata(IMetaObject* parentObj);
};