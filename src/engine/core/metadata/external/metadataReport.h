#include "metadata/metadata.h"
#include "metadata/metaObjects/objects/dataReport.h"

#define sign_dataReport 0x2355F6421261D

class CMetadataReport : public IMetadata
{
	wxString m_fullPath;

	IMetadata* m_ownerMeta; //owner for saving/loading
	CMetaObjectReport* m_commonObject; 	//common meta object

	version_identifier_t m_version;

public:

	CMetadataReport();
	CMetadataReport(CMetaObjectReport* dataProcessor);
	virtual ~CMetadataReport();

	virtual CMetaObjectReport* GetReport() const { return m_commonObject; }
	virtual IModuleManager* GetModuleManager() const { return m_moduleManager; }

	virtual void SetVersion(const version_identifier_t &version) { m_version = version; }
	virtual version_identifier_t GetVersion() const { return m_version; }

	virtual wxString GetFileName() const { return m_fullPath; }

	//runtime support:
	virtual CValue* CreateObjectRef(const wxString& className, CValue** aParams = NULL);
	virtual CLASS_ID GetIDObjectFromString(const wxString& clsName) const;
	virtual wxString GetNameObjectFromID(const CLASS_ID& clsid, bool upper = false) const;
	virtual IMetaTypeObjectValueSingle* GetTypeObject(const IMetaObject* metaValue, enum eMetaObjectType refType) const;
	virtual wxArrayString GetAvailableObjects(enum eMetaObjectType refType) const;

	virtual OptionList* GetTypelist() const;

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

	virtual IMetaObject* GetCommonMetaObject() const { return m_commonObject; }

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