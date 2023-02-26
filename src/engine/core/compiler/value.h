#ifndef _VALUE_H__
#define _VALUE_H__

#include "core/core.h"
#include "core/compiler/compiler.h"
#include "singleObject.h"

#define s_def_alias (int)-1

//simple type date
class CORE_API CValue : public wxObject {
	wxDECLARE_DYNAMIC_CLASS(CValue);
public:
	bool m_bReadOnly;
	//ATTRIBUTES:
	eValueTypes m_typeClass;
	union {
		bool          m_bData;  //TYPE_BOOL
		number_t      m_fData;  //TYPE_NUMBER
		wxLongLong_t  m_dData;  //TYPE_DATE
		CValue* m_pRef;	//TYPE_REFFER
	};
	wxString m_sData;  //TYPE_STRING
public:

	class CORE_API CMethodHelper {
		//Список ключевых слов, которые не могут быть именами переменных и функций
		struct field_t {
			enum eFieldType {
				eConstructor,
				eProperty,
				eMethod
			};
		public:
			eFieldType m_fieldType;
			struct field_data_t {
				struct constructor_data_t {
					wxString m_helperStr;
					long m_paramCount = 0;
				} m_constructor_data;
				struct prop_data_t {
					wxString m_fieldName;
					bool m_readable = true;
					bool m_writable = true;
				} m_prop_data;
				struct method_data_t {
					wxString m_fieldName;
					wxString m_helperStr;
					long m_paramCount = 0;
					bool m_hasRet = true;
				} m_method_data;
			public:
				field_data_t(const field_data_t& data) : m_constructor_data(data.m_constructor_data), m_prop_data(data.m_prop_data), m_method_data(data.m_method_data) {}
				field_data_t(const wxString& helperStr, const long paramCount) : m_constructor_data({ helperStr }) {}
				field_data_t(const wxString& propName, bool readable, bool writable) : m_prop_data({ propName, readable, writable }) {}
				field_data_t(const wxString& methodName, const wxString& helperStr, const long paramCount, bool hasRet) : m_method_data({ methodName, helperStr, paramCount, hasRet }) {}
				~field_data_t() {}
				field_data_t& field_data_t::operator =(const field_data_t& data) {
					m_constructor_data = data.m_constructor_data;
					m_prop_data = data.m_prop_data;
					m_method_data = data.m_method_data;
					return *this;
				}
			} m_field_data;
			long m_lAlias, m_lData;
		public:
			field_t(const field_t& field)
				: m_fieldType(field.m_fieldType), m_field_data(field.m_field_data), m_lAlias(field.m_lAlias), m_lData(field.m_lData) {
			}
			field_t(const wxString& helperStr, const long paramCount, const long lPropAlias = s_def_alias, const long lData = wxNOT_FOUND)
				: m_fieldType(eFieldType::eConstructor), m_field_data(helperStr, paramCount), m_lAlias(lPropAlias), m_lData(lData) {
			}
			field_t(const wxString& propName, bool readable, bool writable, const long lPropAlias = s_def_alias, const long lData = wxNOT_FOUND)
				: m_fieldType(eFieldType::eProperty), m_field_data(propName, readable, writable), m_lAlias(lPropAlias), m_lData(lData) {
			}
			field_t(const wxString& methodName, const wxString& helperStr, const long paramCount, bool hasRet, const long lPropAlias = s_def_alias, const long lData = wxNOT_FOUND)
				: m_fieldType(eFieldType::eMethod), m_field_data(methodName, helperStr, paramCount, hasRet), m_lAlias(lPropAlias), m_lData(lData) {
			}
			field_t& field_t::operator =(const field_t& field) {
				m_fieldType = field.m_fieldType;
				m_field_data = field.m_field_data;
				m_lAlias = field.m_lAlias;
				m_lData = field.m_lData;
				return *this;
			}
		};
		// constructors & props & methods
		std::vector<field_t> m_constructorHelper;	// дерево наименований конструкторов
		std::vector<field_t> m_methodHelper;			// дерево наименований методов
		std::vector<field_t> m_propHelper;			// дерево наименований атрибутов
	public:

		CMethodHelper() {}

		void ClearHelper() {
			m_constructorHelper.clear();
			m_propHelper.clear();
			m_methodHelper.clear();
		}

		long AppendConstructor(const wxString& helperStr) {
			return AppendConstructor(0, helperStr, s_def_alias, wxNOT_FOUND);
		}

		long AppendConstructor(const wxString& helperStr, const long lCtorNum) {
			return AppendConstructor(0, helperStr, s_def_alias, lCtorNum);
		}

		long AppendConstructor(const long paramCount, const wxString& helperStr, const long lCtorNum) {
			return AppendConstructor(paramCount, helperStr, s_def_alias, lCtorNum);
		}

		long AppendConstructor(const long paramCount, const wxString& helperStr) {
			return AppendConstructor(paramCount, helperStr, s_def_alias, wxNOT_FOUND);
		}

		long AppendConstructor(const long paramCount, const wxString& helperStr, const long lCtorNum, const long lCtorAlias) {
			auto itFounded = std::find_if(m_constructorHelper.begin(), m_constructorHelper.end(),
				[paramCount, helperStr](field_t& field) {
					return helperStr.CompareTo(field.m_field_data.m_constructor_data.m_helperStr, wxString::ignoreCase) == 0;
				}
			);

			if (itFounded != m_constructorHelper.end())
				return std::distance(m_constructorHelper.begin(), itFounded);

			m_constructorHelper.emplace_back(
				helperStr,
				paramCount,
				lCtorAlias,
				lCtorNum
			);

			return m_constructorHelper.size();
		}

		void CopyConstructor(const CMethodHelper* src, const long lCtorNum) {
			if (lCtorNum < src->GetNConstructors()) {
				m_constructorHelper.emplace_back(
					src->m_constructorHelper[lCtorNum]
				);
			}
		}

		wxString GetConstructorHelper(const long lCtorNum) const {
			if (lCtorNum > GetNConstructors())
				return wxEmptyString;
			return m_constructorHelper[lCtorNum].m_field_data.m_constructor_data.m_helperStr;
		}

		long GetConstructorAlias(const long lCtorNum) const {
			if (lCtorNum > GetNConstructors())
				return s_def_alias;
			return m_constructorHelper[lCtorNum].m_lAlias;
		}

		long GetConstructorData(const long lCtorNum) const {
			if (lCtorNum > GetNConstructors())
				return wxNOT_FOUND;
			return m_constructorHelper[lCtorNum].m_lData;
		}

		const long int GetNConstructors() const noexcept {
			return m_constructorHelper.size();
		}

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////

		long AppendProp(const wxString& propName) {
			return AppendProp(propName, true, true, wxNOT_FOUND, s_def_alias);
		}

		long AppendProp(const wxString& propName,
			const long lPropNum) {
			return AppendProp(propName, true, true, lPropNum, s_def_alias);
		}

		long AppendProp(const wxString& propName,
			const long lPropNum, const long lPropAlias) {
			return AppendProp(propName, true, true, lPropNum, lPropAlias);
		}

		long AppendProp(const wxString& propName,
			bool readable, const long lPropNum, const long lPropAlias) {
			return AppendProp(propName, readable, true, lPropNum, lPropAlias);
		}

		long AppendProp(const wxString& propName,
			bool readable, bool writable, const long lPropNum) {
			return AppendProp(propName, readable, writable, lPropNum, s_def_alias);
		}

		long AppendProp(const wxString& propName,
			bool readable, bool writable, const long lPropNum, const long lPropAlias) {

			auto itFounded = std::find_if(m_propHelper.begin(), m_propHelper.end(),
				[propName](field_t& field) {
					return propName.CompareTo(field.m_field_data.m_prop_data.m_fieldName, wxString::ignoreCase) == 0;
				}
			);

			if (itFounded != m_propHelper.end())
				return std::distance(m_propHelper.begin(), itFounded);

			m_propHelper.emplace_back(
				propName,
				readable,
				writable,
				lPropAlias,
				lPropNum
			);

			return m_propHelper.size();
		}

		void CopyProp(const CMethodHelper* src, const long lPropNum) {
			if (lPropNum < src->GetNProps()) {
				m_propHelper.emplace_back(
					src->m_propHelper[lPropNum]
				);
			}
		}

		void RemoveProp(const wxString& propName) {
			auto itFounded = std::find_if(m_propHelper.begin(), m_propHelper.end(),
				[propName](field_t& field) {
					return propName.CompareTo(field.m_field_data.m_prop_data.m_fieldName, wxString::ignoreCase) == 0;
				}
			);
			if (itFounded != m_propHelper.end()) {
				m_propHelper.erase(itFounded);
			}
		}

		long FindProp(const wxString& propName) const {
			auto itFounded = std::find_if(m_propHelper.begin(), m_propHelper.end(),
				[propName](const field_t& field) {
					return propName.CompareTo(field.m_field_data.m_prop_data.m_fieldName, wxString::ignoreCase) == 0;
				}
			);
			if (itFounded != m_propHelper.end())
				return std::distance(m_propHelper.begin(), itFounded);
			return wxNOT_FOUND;
		}

		wxString GetPropName(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return wxEmptyString;
			return m_propHelper[lPropNum].m_field_data.m_prop_data.m_fieldName;
		}

		long GetPropAlias(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return wxNOT_FOUND;
			return m_propHelper[lPropNum].m_lAlias;
		}

		long GetPropData(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return wxNOT_FOUND;
			return m_propHelper[lPropNum].m_lData;
		}

		virtual bool IsPropReadable(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return false;
			return m_propHelper[lPropNum].m_field_data.m_prop_data.m_readable;
		}

		virtual bool IsPropWritable(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return false;
			return m_propHelper[lPropNum].m_field_data.m_prop_data.m_writable;
		}

		const long GetNProps() const noexcept {
			return m_propHelper.size();
		}

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////

		long AppendProc(const wxString& methodName) {
			return AppendMethod(methodName, wxEmptyString, 0, false, s_def_alias, wxNOT_FOUND);
		}

		long AppendProc(const wxString& methodName,
			const wxString& helperStr) {
			return AppendMethod(methodName, helperStr, 0, false, s_def_alias, wxNOT_FOUND);
		}

		long AppendProc(const wxString& methodName,
			const wxString& helperStr, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, 0, false, lMethodNum, lMethodAlias);
		}

		long AppendProc(const wxString& methodName,
			const wxString& helperStr, const long paramCount, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, paramCount, false, lMethodNum, lMethodAlias);
		}

		long AppendProc(const wxString& methodName,
			const long paramCount, const wxString& helperStr) {
			return AppendMethod(methodName, helperStr, paramCount, false, wxNOT_FOUND, s_def_alias);
		}

		long AppendProc(const wxString& methodName,
			const long paramCount, const wxString& helperStr, const long lMethodNum) {
			return AppendMethod(methodName, helperStr, paramCount, false, lMethodNum, s_def_alias);
		}

		long AppendProc(const wxString& methodName,
			const long paramCount, const wxString& helperStr, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, paramCount, false, lMethodNum, lMethodAlias);
		}

		long AppendFunc(const wxString& methodName) {
			return AppendMethod(methodName, wxEmptyString, 0, true, wxNOT_FOUND, s_def_alias);
		}

		long AppendFunc(const wxString& methodName,
			const wxString& helperStr) {
			return AppendMethod(methodName, helperStr, 0, true, wxNOT_FOUND, s_def_alias);
		}

		long AppendFunc(const wxString& methodName,
			const long paramCount, const wxString& helperStr) {
			return AppendMethod(methodName, helperStr, paramCount, true, wxNOT_FOUND, s_def_alias);
		}

		long AppendFunc(const wxString& methodName,
			const wxString& helperStr, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, 0, true, lMethodNum, lMethodAlias);
		}

		long AppendFunc(const wxString& methodName,
			const wxString& helperStr, const long paramCount, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, paramCount, true, lMethodNum, lMethodAlias);
		}

		long AppendFunc(const wxString& methodName,
			const long paramCount, const wxString& helperStr, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, paramCount, true, wxNOT_FOUND, lMethodAlias);
		}

		long AppendFunc(const wxString& methodName,
			const long paramCount, const wxString& helperStr, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, helperStr, paramCount, true, lMethodNum, lMethodAlias);
		}

		long AppendMethod(const wxString& methodName,
			const long paramCount, bool hasRet, const long lMethodNum, const long lMethodAlias) {
			return AppendMethod(methodName, wxEmptyString, paramCount, hasRet, lMethodNum, lMethodAlias);
		}

		long AppendMethod(const wxString& methodName, const wxString& helperStr, const long paramCount, bool hasRet, const long lMethodNum, const long lMethodAlias) {
			auto itFounded = std::find_if(m_methodHelper.begin(), m_methodHelper.end(),
				[methodName](const field_t& field) {
					return methodName.CompareTo(field.m_field_data.m_method_data.m_fieldName, wxString::ignoreCase) == 0;
				}
			);
			
			if (itFounded != m_methodHelper.end())
				return std::distance(m_methodHelper.begin(), itFounded);

			m_methodHelper.emplace_back(
				methodName,
				helperStr,
				paramCount,
				hasRet,
				lMethodAlias,
				lMethodNum
			);

			return m_methodHelper.size();
		}

		void CopyMethod(const CMethodHelper* src, const long lMethodNum) {
			if (lMethodNum < src->GetNMethods()) {
				m_methodHelper.emplace_back(
					src->m_methodHelper[lMethodNum]
				);
			}
		}

		void RemoveMethod(const wxString& methodName) {
			auto itFounded = std::find_if(m_methodHelper.begin(), m_methodHelper.end(),
				[methodName](field_t& field) {
					return methodName.CompareTo(field.m_field_data.m_method_data.m_fieldName, wxString::ignoreCase) == 0;
				}
			);
			if (itFounded != m_methodHelper.end()) {
				m_methodHelper.erase(itFounded);
			}
		}

		long FindMethod(const wxString& methodName) const {
			auto itFounded = std::find_if(m_methodHelper.begin(), m_methodHelper.end(),
				[methodName](const field_t& field) {
					return methodName.CompareTo(field.m_field_data.m_method_data.m_fieldName, wxString::ignoreCase) == 0;
				}
			);
			if (itFounded != m_methodHelper.end())
				return std::distance(m_methodHelper.begin(), itFounded);
			return wxNOT_FOUND;
		}

		wxString GetMethodName(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxEmptyString;
			return m_methodHelper[lMethodNum].m_field_data.m_method_data.m_fieldName;
		}

		wxString GetMethodHelper(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxEmptyString;
			return m_methodHelper[lMethodNum].m_field_data.m_method_data.m_helperStr;
		}

		long GetMethodAlias(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxNOT_FOUND;
			return m_methodHelper[lMethodNum].m_lAlias;
		}

		long GetMethodData(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxNOT_FOUND;
			return m_methodHelper[lMethodNum].m_lData;
		}

		bool HasRetVal(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return true;
			return m_methodHelper[lMethodNum].m_field_data.m_method_data.m_hasRet;
		}

		long GetNParams(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxNOT_FOUND;
			return m_methodHelper[lMethodNum].m_field_data.m_method_data.m_paramCount;
		}

		const long GetNMethods() const noexcept {
			return m_methodHelper.size();
		}
	};

public:

	//METHODS:
	//constructors:
	CValue();
	CValue(const CValue& cParam);
	CValue(CValue* pParam);
	CValue(eValueTypes nType, bool readOnly = false);

	//конструкторы копирования:
	CValue(bool cParam); //boolean 
	CValue(signed int cParam); //number 
	CValue(unsigned int cParam); //number
	CValue(double cParam); //number 
	CValue(const number_t& cParam); //number 
	CValue(wxLongLong_t cParam); //date 
	CValue(const wxDateTime& cParam); //date 
	CValue(int nYear, int nMonth, int nDay, unsigned short nHour = 0, unsigned short nMinute = 0, unsigned short nSecond = 0); //date 
	CValue(const wxString& sParam); //string 
	CValue(char* sParam); //string 

	//деструктор:
	virtual ~CValue();

	//Очистка значений
	inline void Reset();

	//Ref counter
	unsigned int GetRefCount() const {
		return m_refCount;
	}

	virtual void IncrRef() { m_refCount++; }
	virtual void DecrRef();

	//операторы:
	void operator = (const CValue& cParam);

	void operator = (bool cParam);
	void operator = (int cParam);
	void operator = (unsigned int cParam);
	void operator = (double cParam);
	void operator = (const wxString& cParam);
	void operator = (eValueTypes cParam);
	void operator = (CValue* pParam);
	void operator = (const wxDateTime& cParam);
	void operator = (wxLongLong_t cParam);

	//Реализация операторов сравнения:
	bool operator > (const CValue& cParam) const {
		return CompareValueGT(cParam);
	}

	bool operator >= (const CValue& cParam) const {
		return CompareValueGE(cParam);
	}

	bool operator < (const CValue& cParam) const {
		return CompareValueLS(cParam);
	}

	bool operator <= (const CValue& cParam) const {
		return CompareValueLE(cParam);
	}

	bool operator == (const CValue& cParam) const {
		return CompareValueEQ(cParam);
	}

	bool operator != (const CValue& cParam) const {
		return CompareValueNE(cParam);
	}

	const CValue& operator+(const CValue& cParam);
	const CValue& operator-(const CValue& cParam);

	//Реализация операторов сравнения:
	virtual inline bool CompareValueGT(const CValue& cParam) const;
	virtual inline bool CompareValueGE(const CValue& cParam) const;
	virtual inline bool CompareValueLS(const CValue& cParam) const;
	virtual inline bool CompareValueLE(const CValue& cParam) const;
	virtual inline bool CompareValueEQ(const CValue& cParam) const;
	virtual inline bool CompareValueNE(const CValue& cParam) const;

	//special converting
	template <typename valueType> inline valueType* ConvertToType() const {
		return value_cast<valueType>(this);
	}

	template <typename enumType > inline enumType ConvertToEnumType() const {
		class IEnumerationVariant<enumType>* enumValue =
			value_cast<class IEnumerationVariant<enumType>>(this);
		wxASSERT(enumValue);
		return enumValue->GetEnumValue();
	};

	//convert to value
	template <typename retType> inline bool ConvertToValue(retType& refValue) const {
		if (m_typeClass == eValueTypes::TYPE_REFFER) {
			refValue = dynamic_cast<retType> (GetRef());
			return refValue != NULL;
		}
		else if (m_typeClass != eValueTypes::TYPE_EMPTY) {
			CValue* refData = const_cast<CValue*>(this);
			wxASSERT(refData);
			refValue = dynamic_cast<retType> (refData);
			return refValue != NULL;
		}
		return false;
	};

public:

#ifdef DEBUG 
	static void ShowCreatedObject();
#endif 

	//runtime support:
	static CValue CreateObject(const CLASS_ID& clsid, CValue** paParams = NULL, const long lSizeArray = 0) {
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}

	static CValue CreateObject(const wxString& className, CValue** paParams = NULL, const long lSizeArray = 0) {
		return CreateObjectRef(className, paParams, lSizeArray);
	}

	static CValue* CreateObjectRef(const CLASS_ID& clsid, CValue** paParams = NULL, const long lSizeArray = 0);
	static CValue* CreateObjectRef(const wxString& className, CValue** paParams = NULL, const long lSizeArray = 0) {
		return CreateObjectRef(
			GetIDObjectFromString(className), paParams, lSizeArray
		);
	}

	template<class retType = CValue>
	static retType* CreateAndConvertObjectRef(const CLASS_ID& clsid, CValue** paParams = NULL, const long lSizeArray = 0) {
		return value_cast<retType>(CreateObjectRef(clsid, paParams, lSizeArray));
	}

	template<class retType = CValue>
	static retType* CreateAndConvertObjectRef(const wxString& className, CValue** paParams = NULL, const long lSizeArray = 0) {
		return value_cast<retType>(CreateObjectRef(className, paParams, lSizeArray));
	}

	static void RegisterObject(const wxString& className, IObjectValueAbstract* singleObject);
	static void UnRegisterObject(const wxString& className);

	static bool IsRegisterObject(const wxString& className);
	static bool IsRegisterObject(const wxString& className, eObjectType objectType);
	static bool IsRegisterObject(const CLASS_ID& clsid);

	static CLASS_ID GetTypeIDByRef(const wxClassInfo* classInfo);
	static CLASS_ID GetTypeIDByRef(const CValue* objectRef);

	static CLASS_ID GetIDObjectFromString(const wxString& className);
	static bool CompareObjectName(const wxString& className, eValueTypes valueType);
	static wxString GetNameObjectFromID(const CLASS_ID& clsid, bool upper = false);
	static wxString GetNameObjectFromVT(eValueTypes valueType, bool upper = false);
	static eValueTypes GetVTByID(const CLASS_ID& clsid);
	static CLASS_ID GetIDByVT(const eValueTypes& valueType);

	static IObjectValueAbstract* GetAvailableObject(const CLASS_ID& clsid);
	static IObjectValueAbstract* GetAvailableObject(const wxString& className);

	static wxArrayString GetAvailableObjects(eObjectType objectType = eObjectType::eObjectType_object);

	//static event 
	static void OnRegisterObject(const wxString& className, IObjectValueAbstract* singleObject) {}
	static void OnUnRegisterObject(const wxString& className) {}

public:

	//special copy function
	inline void Copy(const CValue& cOld);

	void FromDate(int& nYear, int& nMonth, int& nDay) const;
	void FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const;
	void FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const;

	//Виртуальные методы:
	virtual void SetType(eValueTypes type);
	virtual eValueTypes GetType() const;

	virtual inline bool IsEmpty() const;

	virtual wxString GetTypeString() const;
	virtual CLASS_ID GetTypeClass() const;

	virtual bool Init() {
		if (m_pRef != NULL && m_typeClass == eValueTypes::TYPE_REFFER)
			return m_pRef->Init();
		return true;
	}

	virtual bool Init(CValue** paParams, const long lSizeArray) {
		if (m_pRef != NULL && m_typeClass == eValueTypes::TYPE_REFFER)
			return m_pRef->Init(paParams, lSizeArray);
		return true;
	}

	virtual void SetValue(const CValue& varValue);

	virtual bool SetBoolean(const wxString& strValue);
	virtual bool SetNumber(const wxString& strValue);
	virtual bool SetDate(const wxString& strValue);
	virtual bool SetString(const wxString& strValue);

	virtual bool FindValue(const wxString& findData, std::vector<CValue>& foundedObjects) const;

	void SetData(const CValue& varValue); //установка значения без изменения типа

	virtual CValue GetValue(bool getThis = false);

	virtual bool GetBoolean() const;
	virtual int GetInteger() const {
		return GetNumber().ToInt();
	}

	virtual unsigned int GetUInteger() const {
		return GetNumber().GetUInteger();
	}

	virtual double GetDouble() const {
		return GetNumber().ToDouble();
	}

	virtual wxDateTime GetDateTime() const {
		return wxLongLong(GetDate());
	}

	virtual number_t GetNumber() const;
	virtual wxString GetString() const;
	virtual wxLongLong_t GetDate() const;

	/////////////////////////////////////////////////////////////////////////

	virtual CValue* GetRef() const;

	/////////////////////////////////////////////////////////////////////////

	virtual void ShowValue();

	/////////////////////////////////////////////////////////////////////////

#pragma region attributes

	virtual CMethodHelper* GetPMethods() const {
		return NULL;
	}

	virtual void PrepareNames() const;

	/// Returns number of component properties
	/**
	*  @return number of properties
	*/
	virtual long GetNProps() const;

	/// Finds property by name
	/**
	 *  @param wsPropName - property name
	 *  @return property index or -1, if it is not found
	 */
	virtual long FindProp(const wxString& PropName) const;

	/// Returns property name
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return proeprty name or 0 if it is not found
	 */
	virtual wxString GetPropName(const long lPropNum) const;

	/// Returns property value
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return the result of
	 */
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);

	/// Sets the property value
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @param varPropVal - the pointer to a variable for property value
	 *  @return the result of
	 */
	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);

	/// Is property readable?
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return true if property is readable
	 */
	virtual bool IsPropReadable(const long lPropNum) const;

	/// Is property writable?
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return true if property is writable
	 */
	virtual bool IsPropWritable(const long lPropNum) const;

	/// Returns number of component methods
	/**
	 *  @return number of component  methods
	 */
	virtual long GetNMethods() const;

	/// Finds a method by name 
	/**
	 *  @param wsMethodName - method name
	 *  @return - method index
	 */
	virtual long FindMethod(const wxString& methodName) const;

	/// Returns method name
	/**
	 *  @param lMethodNum - method index(starting with 0)
	 *  @return method name or 0 if method is not found
	 */
	virtual wxString GetMethodName(const long lMethodNum) const;

	/// Returns method helper
	/**
	*  @param lMethodNum - method index(starting with 0)
	*  @return method name or 0 if method is not found
	*/
	virtual wxString GetMethodHelper(const long lMethodNum) const;

	/// Returns number of method parameters
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @return number of parameters
	 */
	virtual long GetNParams(const long lMethodNum) const;

	/// Returns default value of method parameter
	/**
	 *  @param lMethodNum - method index(starting with 0)
	 *  @param lParamNum - parameter index (starting with 0)
	 *  @param pvarParamDefValue - the pointer to a variable for default value
	 *  @return the result of
	 */
	virtual bool GetParamDefValue(const long lMethodNum,
		const long lParamNum,
		CValue& pvarParamDefValue) const;

	/// Does the method have a return value?
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @return true if the method has a return value
	 */
	virtual bool HasRetVal(const long lMethodNum) const;

	/// Calls the method as a procedure
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @param paParams - the pointer to array of method parameters
	 *  @param lSizeArray - the size of array
	 *  @return the result of
	 */
	virtual bool CallAsProc(const long lMethodNum,
		CValue** paParams,
		const long lSizeArray);

	/// Calls the method as a function
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @param pvarRetValue - the pointer to returned value
	 *  @param paParams - the pointer to array of method parameters
	 *  @param lSizeArray - the size of array
	 *  @return the result of
	 */
	virtual bool CallAsFunc(const long lMethodNum,
		CValue& pvarRetValue,
		CValue** paParams,
		const long lSizeArray);

#pragma endregion

	CValue* GetThis() {
		return this;
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
	virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

	CValue operator[](const CValue& varKeyValue) {
		CValue retValue; 
		GetAt(varKeyValue, retValue);
		return retValue;
	}

#pragma region iterator 
	virtual CValue GetItEmpty();
	virtual CValue GetItAt(unsigned int idx);
	virtual unsigned int GetItSize() const;

	virtual bool HasIterator() const;
#pragma endregion

private:

	unsigned int m_refCount;
};

//*************************************************************************************************************************************
//*                                                           value casting                                                           *
//*************************************************************************************************************************************

extern CORE_API void ThrowErrorTypeOperation(const wxString& fromType, wxClassInfo* clsInfo);

template <typename retType,
	typename retRef = retType* >
class value_cast {
	CValue* m_castValue;
public:

	operator retRef() const {
		return cast_value();
	}

	explicit value_cast(const CValue& cValue) :
		m_castValue(cValue.GetRef()) {
	}

	explicit value_cast(const CValue* refValue) :
		m_castValue(refValue ? refValue->GetRef() : NULL) {
	}

protected:

	inline retRef cast_value() const {
		retRef retValue = NULL;
		if (m_castValue != NULL) {
			retValue = dynamic_cast<retRef>(m_castValue);
			if (retValue != NULL) {
				return retValue;
			}
		}
#if defined(_USE_CONTROL_VALUECAST)
		//if (m_castValue->GetType() == eValueTypes::TYPE_EMPTY)
		//	return NULL;
		ThrowErrorTypeOperation(
			m_castValue ? m_castValue->GetTypeString() : wxEmptyString,
			CLASSINFO(retType)
		);
#endif
		return NULL;
	}
};

//*******************************************************************************************
//*                                 Register new objects                                    *
//*******************************************************************************************

class CORE_API value_register {
	IObjectValueSingle* m_so;
public:
	value_register(IObjectValueSingle* so) : m_so(so) {
		try {
			if (m_so != NULL)
				CValue::RegisterObject(m_so->GetClassName(), so);
		}
		catch (...) {
			m_so = NULL;
		}
	}
	~value_register() {
		try {
			if (m_so != NULL)
				CValue::UnRegisterObject(m_so->GetClassName());
		}
		catch (...) {
		}
	}
};

#define GENERATE_REGISTER(class_name, class_type, class_so)\
	static const value_register class_type = class_so;

//*******************************************************************************************

#define S_VALUE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_s_), new CSimpleObjectValueSingle<class_info>(wxT(class_name), class_type, clsid))

#define SO_VALUE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_so_), new CSystemObjectValueSingle<class_info>(wxT(class_name), clsid))

#define VALUE_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_val_), new CObjectValueSingle<class_info>(wxT(class_name), clsid))

#define CONTROL_VALUE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_c_), new CControlObjectValueSingle<class_info>(wxT(class_name), wxT(class_type), clsid))

#define S_CONTROL_VALUE_REGISTER(class_info, class_name, class_type, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_sc_), new CControlObjectValueSingle<class_info>(wxT(class_name), wxT(class_type), clsid, true))

#define ENUM_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_e_), new CEnumObjectValueSingle<class_info>(wxT(class_name), clsid))

#define METADATA_REGISTER(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_m_), new CMetaObjectValueSingle<class_info>(wxT(class_name), clsid))

#endif 