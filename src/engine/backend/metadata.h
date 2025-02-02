#ifndef _METADATA_H__
#define _METADATA_H__

#include "backend/moduleManager/moduleManager.h"

///////////////////////////////////////////////////////////////////////////////

class IMetaValueTypeCtor;
class BACKEND_API IBackendMetadataTree;

///////////////////////////////////////////////////////////////////////////////

#define wxOES_Data wxT("OES_Data")

///////////////////////////////////////////////////////////////////////////////

class BACKEND_API IMetaData {
	IBackendMetadataTree* m_metaTree;
private:
	void DoGenerateNewID(meta_identifier_t& id, IMetaObject* top) const;
	//Get metaobjects 
	void DoGetMetaObject(std::vector<IMetaObject*>& metaObjects, const IMetaObject* top) const;
	void DoGetMetaObject(const class_identifier_t& clsid, 
		std::vector<IMetaObject*>& metaObjects, const IMetaObject* top) const;
	//find object
	IMetaObject* DoFindByName(const wxString& fullName, IMetaObject* top) const;
	//get metaObject 
	IMetaObject* DoGetMetaObject(const meta_identifier_t& id, IMetaObject* top) const;
	IMetaObject* DoGetMetaObject(const Guid& guid, IMetaObject* top) const;
public:
	IMetaData(bool readOnly = false) :
		m_metaTree(nullptr),
		m_metaReadOnly(readOnly),
		m_metaModify(false) {
	}
	virtual ~IMetaData() {}

	virtual IModuleManager* GetModuleManager() const = 0;

	virtual bool IsModified() const {
		return m_metaModify;
	}

	virtual void Modify(bool modify = true) {
		if (m_metaTree != nullptr)
			m_metaTree->Modify(modify);
		m_metaModify = modify;
	}

	virtual void SetVersion(const version_identifier_t& version) = 0;
	virtual version_identifier_t GetVersion() const = 0;

	virtual wxString GetFileName() const {
		return wxEmptyString;
	}

	virtual IMetaObject* GetCommonMetaObject() const {
		return nullptr;
	}

	//runtime support:
	inline CValue CreateObject(const class_identifier_t& clsid, CValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	inline CValue CreateObject(const wxString& className, CValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(className, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	inline CValue CreateObjectValue(Args&&... args) {
		return CreateObjectValueRef<T>(std::forward<Args>(args)...);
	}

	inline CValue* CreateObjectRef(const class_identifier_t& clsid, CValue** paParams = nullptr, const long lSizeArray = 0);
	inline CValue* CreateObjectRef(const wxString& className, CValue** paParams = nullptr, const long lSizeArray = 0) {
		const class_identifier_t& clsid = GetIDObjectFromString(className);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	inline CValue* CreateObjectValueRef(Args&&... args) {
		return CreateAndConvertObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<class retType = CValue>
	inline retType* CreateAndConvertObjectRef(const class_identifier_t& clsid, CValue** paParams = nullptr, const long lSizeArray = 0) {
		return value_cast<retType>(CreateObjectRef(clsid, paParams, lSizeArray));
	}
	template<class retType = CValue>
	inline retType* CreateAndConvertObjectRef(const wxString& className, CValue** paParams = nullptr, const long lSizeArray = 0) {
		return value_cast<retType>(CreateObjectRef(className, paParams, lSizeArray));
	}
	template<typename T, typename... Args>
	inline T* CreateAndConvertObjectValueRef(Args&&... args) {
		auto ptr = static_cast<T*>(malloc(sizeof(T)));
		T* created_value = ::new (ptr) T(std::forward<Args>(args)...);
		if (created_value == nullptr) 
			return nullptr;
		if (!IsRegisterCtor(created_value->GetClassType())) {
			wxDELETE(created_value);
			wxASSERT_MSG(false, "CreateAndConvertObjectValueRef ret null!");
			return nullptr;
		}
		created_value->PrepareNames();
		return created_value;
	}

	void RegisterCtor(IMetaValueTypeCtor* typeCtor);
	void UnRegisterCtor(IMetaValueTypeCtor*& typeCtor);

	void UnRegisterCtor(const wxString& className);

	virtual bool IsRegisterCtor(const wxString& className) const;
	virtual bool IsRegisterCtor(const wxString& className, eCtorObjectType objectType) const;
	virtual bool IsRegisterCtor(const wxString& className, eCtorObjectType objectType, enum eCtorMetaType metaType) const;

	virtual bool IsRegisterCtor(const class_identifier_t& clsid) const;

	virtual class_identifier_t GetIDObjectFromString(const wxString& clsName) const;
	virtual wxString GetNameObjectFromID(const class_identifier_t& clsid, bool upper = false) const;

	inline meta_identifier_t GetVTByID(const class_identifier_t& clsid) const;
	inline class_identifier_t GetIDByVT(const meta_identifier_t& valueType, enum eCtorMetaType refType) const;

	virtual IMetaValueTypeCtor* GetTypeCtor(const class_identifier_t& clsid) const;
	virtual IMetaValueTypeCtor* GetTypeCtor(const IMetaObject* metaValue, enum eCtorMetaType refType) const;

	virtual IAbstractTypeCtor* GetAvailableCtor(const class_identifier_t& clsid) const;
	virtual IAbstractTypeCtor* GetAvailableCtor(const wxString& className) const;

	virtual std::vector<IMetaValueTypeCtor*> GetListCtorsByType() const;
	virtual std::vector<IMetaValueTypeCtor*> GetListCtorsByType(const class_identifier_t& clsid, enum eCtorMetaType refType) const;
	virtual std::vector<IMetaValueTypeCtor*> GetListCtorsByType(enum eCtorMetaType refType) const;

	//run/close 
	virtual bool RunConfiguration(int flags = defaultFlag) = 0;
	virtual bool CloseConfiguration(int flags = defaultFlag) = 0;

	//metaobject
	IMetaObject* CreateMetaObject(const class_identifier_t& clsid, IMetaObject* parentMetaObj);

	bool RenameMetaObject(IMetaObject* obj, const wxString& sNewName);
	void RemoveMetaObject(IMetaObject* obj, IMetaObject* objParent = nullptr);

	//Get metaobjects 
	virtual std::vector<IMetaObject*> GetMetaObject(const IMetaObject* top = nullptr) const;
	virtual std::vector<IMetaObject*> GetMetaObject(const class_identifier_t& clsid, const IMetaObject* top = nullptr) const;

	//find object
	virtual IMetaObject* FindByName(const wxString& fullName) const;

	//get metaObject 
	template <typename retType>
	inline bool GetMetaObject(retType*& foundedVal, const meta_identifier_t& id, IMetaObject* top = nullptr) const {
		foundedVal = dynamic_cast<retType*>(GetMetaObject(id, top));
		return foundedVal != nullptr;
	}

	template <typename retType>
	inline bool GetMetaObject(retType*& foundedVal, const Guid& guid, IMetaObject* top = nullptr) const {
		foundedVal = dynamic_cast<retType*>(GetMetaObject(guid, top));
		return foundedVal != nullptr;
	}

	//get metaObject 
	virtual IMetaObject* GetMetaObject(const meta_identifier_t& id, IMetaObject* top = nullptr) const;
	virtual IMetaObject* GetMetaObject(const Guid& guid, IMetaObject* top = nullptr) const;

	//Associate this metaData with 
	virtual IBackendMetadataTree* GetMetaTree() const {
		return m_metaTree;
	}

	virtual void SetMetaTree(IBackendMetadataTree* metaTree) {
		m_metaTree = metaTree;
	}

	//ID's 
	virtual meta_identifier_t GenerateNewID() const;

	//Generate new name
	virtual wxString GetNewName(const class_identifier_t& clsid, IMetaObject* metaParent, const wxString& sPrefix = wxEmptyString, bool forConstructor = false);

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
	std::vector<IMetaValueTypeCtor*> m_factoryCtors;
};

#endif 