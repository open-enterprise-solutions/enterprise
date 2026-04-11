#ifndef __METADATA_H__
#define __METADATA_H__

#include "backend/moduleManager/moduleManager.h"

///////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibBackendMetadataTree;
///////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibCtorMetaValueType;
///////////////////////////////////////////////////////////////////////////////

class BACKEND_API ibMetaData {
	void DoGenerateNewID(ibMetaID& id, ibValueMetaObject* top) const;
public:

	ibMetaData() :
		m_metaModify(false),
		m_metaTree(nullptr) {
	}

	virtual ~ibMetaData() {}

	virtual ibValueModuleManager* GetModuleManager() const = 0;

	virtual bool IsModified() const { return m_metaModify; }
	virtual void Modify(bool modify = true) {
		if (m_metaTree != nullptr)
			m_metaTree->Modify(modify);
		m_metaModify = modify;
	}

	virtual void SetVersion(const ibVersionID& version) = 0;
	virtual ibVersionID GetVersion() const = 0;

	virtual wxString GetFileName() const { return wxEmptyString; }
	virtual ibValueMetaObject* GetCommonMetaObject() const { return nullptr; }

	//runtime support:
	inline ibValue CreateObject(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	inline ibValue CreateObject(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CreateObjectRef(className, paParams, lSizeArray);
	}

	template<typename T, typename... Args>
	inline ibValue CreateObjectValue(Args&&... args) const {
		return CreateObjectValueRef<T>(std::forward<Args>(args)...);
	}

	virtual ibValue* CreateObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const;
	virtual ibValue* CreateObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		const ibClassID& clsid = GetIDObjectFromString(className);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	inline ibValue* CreateObjectValueRef(Args&&... args) const {
		return CreateAndConvertObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<class T = ibValue>
	inline T* CreateAndConvertObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CastValue<T>(CreateObjectRef(clsid, paParams, lSizeArray));
	}
	template<class T = ibValue>
	inline T* CreateAndConvertObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) const {
		return CastValue<T>(CreateObjectRef(className, paParams, lSizeArray));
	}
	template<typename T, typename... Args>
	inline T* CreateAndConvertObjectValueRef(Args&&... args) const {
		T* created_value = ibValue::CreateAndPrepareValueRef<T>(args...);
		if (!IsRegisterCtor(created_value->GetClassType())) {
			wxDELETE(created_value);
			wxASSERT_MSG(false, "CreateAndConvertObjectValueRef ret null!");
			return nullptr;
		}
		return created_value;
	}

	void RegisterCtor(ibCtorMetaValueType* typeCtor);
	void UnRegisterCtor(ibCtorMetaValueType*& typeCtor);

	void UnRegisterCtor(const wxString& className);

	virtual bool IsRegisterCtor(const wxString& className) const;
	virtual bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const;
	virtual bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, enum ibCtorObjectMetaType metaType) const;

	virtual bool IsRegisterCtor(const ibClassID& clsid) const;

	virtual ibClassID GetIDObjectFromString(const wxString& className) const;
	virtual wxString GetNameObjectFromID(const ibClassID& clsid, bool upper = false) const;

	inline ibMetaID GetVTByID(const ibClassID& clsid) const;
	inline ibClassID GetIDByVT(const ibMetaID& valueType, enum ibCtorObjectMetaType refType) const;

	virtual ibCtorMetaValueType* GetTypeCtor(const wxString& className) const;
	virtual ibCtorMetaValueType* GetTypeCtor(const ibClassID& clsid) const;
	virtual ibCtorMetaValueType* GetTypeCtor(const ibValueMetaObject* metaValue, enum ibCtorObjectMetaType refType) const;

	virtual ibCtorAbstractType* GetAvailableCtor(const wxString& className) const;
	virtual ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid) const;

	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType() const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(const ibClassID& clsid, enum ibCtorObjectMetaType refType) const;
	virtual std::vector<ibCtorMetaValueType*> GetListCtorsByType(enum ibCtorObjectMetaType refType) const;

	//get parent metadata 
	virtual bool GetOwner(ibMetaData*& metaData) const { return false; }

	//factory version 
	virtual unsigned int GetFactoryCountChanges() const {
		return m_factoryCtorCountChanges + ibValue::GetFactoryCountChanges();
	}

	//get language code 
	virtual wxString GetLangCode() const = 0;

	//Check is full access 
	virtual bool IsFullAccess() const { return true; }

	//associate this metaData with 
	virtual ibBackendMetadataTree* GetMetaTree() const { return m_metaTree; }
	virtual void SetMetaTree(ibBackendMetadataTree* metaTree) { m_metaTree = metaTree; }

	//run/close 
	virtual bool RunDatabase(int flags = defaultFlag) = 0;
	virtual bool CloseDatabase(int flags = defaultFlag) = 0;

	//metaobject
	ibValueMetaObject* CreateMetaObject(const ibClassID& clsid,
		ibValueMetaObject* parentMetaObj, bool runObject = true);

	bool RenameMetaObject(ibValueMetaObject* object, const wxString& newName);
	void RemoveMetaObject(ibValueMetaObject* object, ibValueMetaObject* objParent = nullptr);

#pragma region __array_h__

	//any  
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(const bool use_child_filter = false) const {
		std::vector<_T1*> array;
		FillArrayObjectByFilter<_T1, ibValueMetaObject>(array, {}, use_child_filter);
		return array;
	}

	//any 
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(const ibClassID& clsid, const bool use_child_filter = false) const {
		std::vector<_T1*> array;
		FillArrayObjectByFilter<_T1, ibValueMetaObject>(array, { clsid });
		return array;
	}

	//any  
	template <typename _T1 = ibValueMetaObject>
	std::vector<_T1*> GetAnyArrayObject(const std::initializer_list<ibClassID> filter, const bool use_child_filter = false) const {
		std::vector<_T1*> array;
		FillArrayObjectByFilter<_T1, ibValueMetaObject>(array, filter, use_child_filter);
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//any 
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, {}, use_child_filter);
	}

	//any 
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id, const ibClassID& clsid, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, _T1>(id, { clsid }, use_child_filter);
	}

	//any 
	template <typename _T1 = ibValueMetaObject, typename _T2>
	_T1* FindAnyObjectByFilter(const _T2& id,
		const std::initializer_list<ibClassID> filter, const bool use_child_filter = false) const {
		return FindObjectByFilter<_T2, ibValueMetaObject, ibValueMetaObject, _T1>(id, filter, use_child_filter);
	}

#pragma endregion 

	//ID's 
	ibMetaID GenerateNewID() const;

	//generate new name
	wxString GetNewName(const ibClassID& clsid,
		ibValueMetaObject* parent, const wxString& strPrefix = wxEmptyString, bool forConstructor = false);

#pragma region serialization

	wxString Serialize(const ibValue& cValue);
	ibValue Deserialize(const wxString& strValue);

#pragma endregion

protected:

#pragma region __array_h__

	template <typename _T1 = ibValueMetaObject, typename _T2 = ibValueMetaObject>
	bool FillArrayObjectByFilter(
		std::vector<_T1*>& array,
		const std::initializer_list<ibClassID> filter) const
	{
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FillArrayObjectByFilter(array, filter, false);
		return false;
	}

	template <typename _T1 = ibValueMetaObject, typename _T2 = ibValueMetaObject>
	bool FillArrayObjectByFilter(
		std::vector<_T1*>& array,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter) const
	{
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FillArrayObjectByFilter(array, filter, use_child_filter);
		return false;
	}

#pragma endregion 
#pragma region __filter_h__

	template<typename _T1, typename _T2 = ibValueMetaObject, typename _T3 = ibValueMetaObject>
	_T3* FindObjectByFilter(
		const _T1& id,
		const std::initializer_list<ibClassID> filter) const {
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FindObjectByFilter<_T3>(id, filter, false);
		return nullptr;
	}

	template<typename _T1, typename _T2 = ibValueMetaObject, typename _T3 = ibValueMetaObject>
	_T3* FindObjectByFilter(
		const _T1& id,
		const std::initializer_list<ibClassID> filter,
		const bool use_child_filter) const {
		const auto commonObject = GetCommonMetaObject();
		if (commonObject != nullptr)
			return commonObject->FindObjectByFilter<_T3>(id, filter, use_child_filter);
		return nullptr;
	}

#pragma endregion 

	enum
	{
		eHeaderBlock = 0x2320,
		eDataBlock = 0x2350,
		eChildBlock = 0x2370
	};

	bool m_metaModify;

	//custom types
	std::vector<ibCtorMetaValueType*> m_factoryCtors;
	std::atomic<unsigned int> m_factoryCtorCountChanges = 0;

private:
	ibBackendMetadataTree* m_metaTree;
};

#endif 