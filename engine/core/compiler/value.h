#ifndef _VALUE_H__
#define _VALUE_H__

#include "core.h"
#include "compiler.h"
#include "singleObject.h"

class CORE_API ITypeValue : public wxObject
{
	wxDECLARE_ABSTRACT_CLASS(ITypeValue);

public:

	ITypeValue(eValueTypes type) : wxObject(), m_typeClass(type) {}

	virtual CValue *GetRef() const = 0;

	//Виртуальные методы:
	virtual void SetType(eValueTypes type);
	virtual eValueTypes GetType() const;

	//Получить тип строки
	virtual wxString GetTypeString() const = 0;

public:

	virtual CLASS_ID GetTypeID() const;

public:

	//ATTRIBUTES:
	eValueTypes m_typeClass;
};

//simple type date
class CORE_API CValue : public ITypeValue {
	wxDECLARE_DYNAMIC_CLASS(CValue);
public:

	bool m_bReadOnly;

	union
	{
		bool          m_bData;  //TYPE_BOOL
		number_t      m_fData;  //TYPE_NUMBER
		wxLongLong_t  m_dData;  //TYPE_DATE
		CValue        *m_pRef;  //TYPE_REFFER
	};

	wxString m_sData;  //TYPE_STRING

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
	CValue(const number_t &cParam); //number 
	CValue(wxLongLong_t cParam); //date 
	CValue(const wxDateTime &cParam); //date 
	CValue(int nYear, int nMonth, int nDay, unsigned short nHour = 0, unsigned short nMinute = 0, unsigned short nSecond = 0); //date 
	CValue(const wxString &sParam); //string 
	CValue(char *sParam); //string 

	//деструктор:
	virtual ~CValue();

	//Очистка значений
	inline void Reset();

	//Ref counter
	void SetRefCount(unsigned int refCount) { m_refCount = refCount; }
	unsigned int GetRefCount() const { return m_refCount; }

	virtual void IncrRef() { m_refCount++; }
	virtual void DecrRef();

	//операторы:
	void operator = (const CValue &cParam);

	void operator = (bool cParam);
	void operator = (int cParam);
	void operator = (unsigned int cParam);
	void operator = (double cParam);
	void operator = (const wxString &cParam);
	void operator = (eValueTypes cParam);
	void operator = (CValue *pParam);
	void operator = (const wxDateTime &cParam);
	void operator = (wxLongLong_t cParam);

	//Реализация операторов сравнения:
	bool operator > (const CValue &cParam) const { return CompareValueGT(cParam); }
	bool operator >= (const CValue &cParam) const { return CompareValueGE(cParam); }
	bool operator < (const CValue &cParam) const { return CompareValueLS(cParam); }
	bool operator <= (const CValue &cParam) const { return CompareValueLE(cParam); }
	bool operator == (const CValue &cParam) const { return CompareValueEQ(cParam); }
	bool operator != (const CValue &cParam) const { return CompareValueNE(cParam); }

	const CValue& operator+(const CValue& cParam);
	const CValue& operator-(const CValue& cParam);

	bool ToBool() const;
	int ToInt() const;
	unsigned int ToUInt() const;
	double ToDouble() const;
	wxLongLong_t ToDate() const;
	wxString ToString() const;

	wxDateTime ToDateTime() const;

	//Реализация операторов сравнения:
	virtual inline bool CompareValueGT(const CValue &cParam) const;
	virtual inline bool CompareValueGE(const CValue &cParam) const;
	virtual inline bool CompareValueLS(const CValue &cParam) const;
	virtual inline bool CompareValueLE(const CValue &cParam) const;
	virtual inline bool CompareValueEQ(const CValue &cParam) const;
	virtual inline bool CompareValueNE(const CValue &cParam) const;

	//special converting
	template <typename valueType> inline valueType *ConvertToType() {
		return value_cast<valueType>(this);
	}

	template <typename enumType > inline enumType ConvertToEnumType()
	{
		class IEnumerationVariant<enumType> *enumValue =
			value_cast<class IEnumerationVariant<enumType>>(this);
		wxASSERT(enumValue);
		return enumValue->GetEnumValue();
	};

	//convert to value
	template <typename retType> inline bool ConvertToValue(retType &refValue) const
	{
		if (m_typeClass == eValueTypes::TYPE_REFFER) {
			refValue = dynamic_cast<retType> (m_pRef);
			return refValue != NULL;
		}
		else if (m_typeClass != eValueTypes::TYPE_EMPTY) {
			CValue *refData = const_cast<CValue *>(this);
			wxASSERT(refData);
			refValue = dynamic_cast<retType> (refData);
			return refValue != NULL;
		}

		return false;
	};

public:

	//runtime support:
	static CValue CreateObject(const wxString &className, CValue **aParams = NULL) { 
		return CreateObjectRef(className, aParams); 
	}
	
	static CValue *CreateObjectRef(const wxString &className, CValue **aParams = NULL);

	template<class retType = CValue>
	static retType* CreateAndConvertObjectRef(const wxString &className, CValue **aParams = NULL) {
		return value_cast<retType>(CreateObjectRef(className, aParams)); 
	}

	static void RegisterObject(const wxString &className, IObjectValueAbstract *singleObject);
	static void UnRegisterObject(const wxString &className);

	static bool IsRegisterObject(const wxString &className);
	static bool IsRegisterObject(const wxString &className, eObjectType objectType);

	static CLASS_ID GetTypeIDByRef(const wxClassInfo *classInfo);
	static CLASS_ID GetTypeIDByRef(const ITypeValue *objectRef);

	static CLASS_ID GetIDObjectFromString(const wxString &className);
	static bool CompareObjectName(const wxString &className, eValueTypes valueType);
	static wxString GetNameObjectFromID(const CLASS_ID &clsid, bool upper = false);
	static wxString GetNameObjectFromVT(eValueTypes valueType, bool upper = false);
	static eValueTypes GetVTByID(const CLASS_ID &clsid);
	static CLASS_ID GetIDByVT(const eValueTypes &valueType);

	static IObjectValueAbstract *GetAvailableObject(const wxString &className);
	static wxArrayString GetAvailableObjects(eObjectType objectType = eObjectType::eObjectType_object);

	//special copy function
	inline void Copy(const CValue& cOld);

	void FromDate(int &nYear, int &nMonth, int &nDay) const;
	void FromDate(int &nYear, int &nMonth, int &nDay, unsigned short &nHour, unsigned short &nMinute, unsigned short &nSecond) const;
	void FromDate(int &nYear, int &nMonth, int &nDay, int &DayOfWeek, int &DayOfYear, int &WeekOfYear) const;

	virtual void Attach(void *pObj);
	virtual void Detach();
	virtual void *GetAttach();

	//Виртуальные методы:
	virtual inline bool IsEmpty() const;
	virtual wxString GetTypeString() const;

	virtual bool Init() { return true; }
	virtual bool Init(CValue **aParams) { return true; }

	virtual void SetValue(const CValue &cVal);

	virtual void SetString(const wxString &sValue);
	virtual void SetNumber(const wxString &sValue);
	virtual void SetBoolean(const wxString &sValue);
	virtual void SetDate(const wxString &Value);

	virtual bool FindValue(const wxString &findData, std::vector<CValue> &foundedObjects);

	void SetData(const CValue &cVal);//установка значения без изменения типа

	virtual CValue GetValue(bool bThis = false);

	virtual bool GetBoolean() const;
	virtual number_t GetNumber() const;
	virtual wxString GetString() const;
	virtual wxLongLong_t GetDate() const;

	virtual CValue *GetRef() const;

	//special
	virtual void SetBinaryData(int nPosition, PreparedStatement *prepare);
	virtual void GetBinaryData(int nPosition, DatabaseResultSet *databaseResultSet);

	//РАБОТА КАК АГРЕГАТНОГО ОБЪЕКТА:
	//эти методы нужно переопределить в ваших агрегатных объектах:
	virtual CMethods* GetPMethods() const { return NULL; }//получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const {}//этот метод автоматически вызывается для инициализации имен атрибутов и методов

	virtual CValue Method(methodArg_t &aParams);
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal);//установка атрибута
	virtual CValue GetAttribute(attributeArg_t &aParams);//значение атрибута

	//а эти методы необязательны для переопределения (т.к. они автоматически поддерживаются данным базовым классом):
	CValue Method(const wxString &sName, CValue **aParams);
	void SetAttribute(const wxString &sName, CValue &cVal);//установка атрибута
	CValue GetAttribute(const wxString &sName);//значение атрибута

	virtual int FindMethod(const wxString &sName) const;
	virtual int FindAttribute(const wxString &sName) const;

	virtual wxString GetMethodName(unsigned int nNumber) const;
	virtual wxString GetMethodDescription(unsigned int nNumber) const;
	virtual wxString GetAttributeName(unsigned int nNumber) const;

	virtual unsigned int GetNMethods() const;
	virtual unsigned int GetNAttributes() const;

	CValue CallFunction(const wxString sName, ...);
	CValue CallFunctionV(const wxString &sName, CValue **p);

	virtual void CheckValue();
	virtual void ShowValue();

	virtual void SetAt(const CValue &cKey, CValue &cVal);
	virtual CValue GetAt(const CValue &cKey);

	CValue operator[](const CValue &cKey) { return GetAt(cKey); }

	virtual CValue GetItEmpty();
	virtual CValue GetItAt(unsigned int idx);
	virtual unsigned int GetItSize() const;

	virtual bool HasIterator() const;

private:

	unsigned int m_refCount;
};

//*************************************************************************************************************************************
//*                                                           CValueNoRet                                                             *
//*************************************************************************************************************************************

class CValueNoRet : public CValue
{
	wxString sProcedure;

public:

	CValueNoRet(const wxString &procedure) : CValue(eValueTypes::TYPE_VALUE, true), sProcedure(procedure) {}
	virtual void CheckValue() override;
};

//*************************************************************************************************************************************
//*                                                         attributeArg_t                                                      *
//*************************************************************************************************************************************

class attributeArg_t {
	int m_index;
	wxString m_name;
private:
	attributeArg_t() {}
	attributeArg_t(const attributeArg_t& src) {}
public:
	attributeArg_t(int index, const wxString &name);
	int GetIndex() const { return m_index; }
	wxString GetName() const { return m_name; }
};

//*************************************************************************************************************************************
//*                                                         methodArg_t                                                         *
//*************************************************************************************************************************************

class methodArg_t {
	CValue **m_aParams;
	unsigned int m_varCount;
	std::set<int> m_setParams;
	int m_index;
	wxString m_name;
private:
	inline void CheckValue(unsigned int idx);
	methodArg_t() {}
	methodArg_t(const methodArg_t&src) {}
public:
	methodArg_t(CValue **params, unsigned int varCount, int iName, const wxString &sName);
	unsigned int GetParamCount() const { return m_varCount; }
	wxString GetName(bool makeUpper = false) const { if (makeUpper) { return m_name.Upper(); } else return m_name; }
	int GetIndex() const { return m_index; }
	CValue *GetAt(unsigned int idx);
	CValue **GetParams() { for (unsigned int i = 0; i < m_varCount; i++) { m_setParams.insert(i); } return m_aParams; }
	CValue operator[](unsigned int idx);
	void CheckParams();
};

//*************************************************************************************************************************************
//*                                                           value casting                                                           *
//*************************************************************************************************************************************

template <typename retType,
	typename retRef = retType * >
	class value_cast {
	CValue *m_castValue;
	public:

		operator retRef() const { return cast_value(); }

		explicit value_cast(const CValue &cValue) : m_castValue(NULL)
		{
			if (cValue.m_typeClass == eValueTypes::TYPE_REFFER) {
				m_castValue = cValue.GetRef();
			}
			else {
				m_castValue = const_cast<CValue *>(&cValue);
			}
		}

		explicit value_cast(CValue *refValue) : m_castValue(NULL)
		{
			if (refValue->m_typeClass == eValueTypes::TYPE_REFFER) {
				m_castValue = refValue->GetRef();
			}
			else {
				m_castValue = refValue;
			}
		}

	protected:

		inline retRef cast_value() const
		{
			retRef retValue = NULL;

			if (m_castValue) {
				retValue = dynamic_cast<retRef>(m_castValue);
				if (retValue != NULL) {
					return retValue;
				}
			}

#if defined(_USE_CONTROL_VALUECAST)
			ThrowErrorTypeOperation(
				m_castValue ? m_castValue->GetTypeString() : wxEmptyString, CLASSINFO(retType)
			);
#endif
			return NULL;
		}
};

#endif 