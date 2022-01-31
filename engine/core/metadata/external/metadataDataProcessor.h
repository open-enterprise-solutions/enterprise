#include "metadata/metadata.h"
#include "metadata/objects/dataProcessor.h"

#define sign_dataProcessor 0x1345F6621261E

class CMetadataDataProcessor : public IMetadata
{
	wxString m_fullPath;

	IMetadata *m_ownerMeta; //owner for saving/loading
	CMetaObjectDataProcessorValue *m_commonObject; 	//common meta object

	version_identifier_t m_version;

public:

	CMetadataDataProcessor();
	CMetadataDataProcessor(CMetaObjectDataProcessorValue *dataProcessor);
	virtual ~CMetadataDataProcessor();

	virtual CMetaObjectDataProcessorValue *GetDataProcessor() const { return m_commonObject; }
	virtual IModuleManager *GetModuleManager() const { return m_moduleManager; }

	virtual void SetVersion(version_identifier_t version) { m_version = version; }
	virtual version_identifier_t GetVersion() const { return m_version; }

	virtual wxString GetFileName() const { return m_fullPath; }

	//runtime support:
	virtual CValue *CreateObjectRef(const wxString &className, CValue **aParams = NULL);
	virtual CLASS_ID GetIDObjectFromString(const wxString &clsName);
	virtual wxString GetNameObjectFromID(const CLASS_ID &clsid, bool upper = false);
	virtual IMetaTypeObjectValueSingle *GetTypeObject(IMetaObject *metaValue, enum eMetaObjectType refType);
	virtual wxArrayString GetAvailableObjects(enum eMetaObjectType refType);

	virtual OptionList *GetTypelist() const;

	//metadata 
	virtual bool CreateMetadata();
	virtual bool LoadMetadata();
	virtual bool SaveMetadata();
	virtual bool ClearMetadata();

	//run/close 
	virtual bool RunMetadata();
	virtual bool CloseMetadata(bool force = false);

	//load/save form file
	bool LoadFromFile(const wxString &fileName);
	bool SaveToFile(const wxString &fileName);

	virtual IMetaObject *GetCommonMetaObject() const { return m_commonObject; }
	
	//get metaObject 
	virtual IMetaObject *GetMetaObject(meta_identifier_t meta_id);

protected:

	//header loader/saver 
	bool LoadHeader(CMemoryReader &readerData);
	bool SaveHeader(CMemoryWriter &writterData);

	//loader/saver/deleter: 
	bool LoadCommonMetadata(const CLASS_ID &clsid, CMemoryReader &readerData);
	bool LoadChildMetadata(const CLASS_ID &clsid, CMemoryReader &readerData, IMetaObject *parentObj);
	bool SaveCommonMetadata(const CLASS_ID &clsid, CMemoryWriter &writterData, bool saveToFile = false);
	bool SaveChildMetadata(const CLASS_ID &clsid, CMemoryWriter &writterData, IMetaObject *parentObj, bool saveToFile);
	bool DeleteCommonMetadata(const CLASS_ID &clsid);
	bool DeleteChildMetadata(const CLASS_ID &clsid, IMetaObject *parentObj);

	//run/close recursively:
	bool RunChildMetadata(IMetaObject *parentObj);
	bool CloseChildMetadata(IMetaObject *parentObj, bool force = false);
	bool ClearChildMetadata(IMetaObject *parentObj);
};