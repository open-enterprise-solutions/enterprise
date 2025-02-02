#ifndef _VALUE_MAP_H__
#define _VALUE_MAP_H__

#include "value.h"
#include <locale>

class BACKEND_API CValueContainer : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueContainer);
private:
	enum Func  {
		enCount = 0,
		enProperty,
		enClear,
		enDelete,
		enInsert
	};
public:

	//јтрибут -> —троковый ключ
	//работа с массивом как с агрегатным объектом:

	virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);
	virtual bool SetAt(const CValue& varKeyValue, const CValue& cValue);

	//check is empty
	virtual inline bool IsEmpty() const { 
		return m_containerValues.empty();
	}

public:

	class BACKEND_API CValueReturnContainer : public CValue {
		
		enum Prop {
			enKey,
			enValue
		};
		
		CValue m_key;
		CValue m_value;
	
		static CMethodHelper m_methodHelper;

	public:

		CValueReturnContainer() : CValue(eValueTypes::TYPE_VALUE, true) { 
			PrepareNames(); 
		}
		
		CValueReturnContainer(const CValue& key, CValue& value) : CValue(eValueTypes::TYPE_VALUE, true), m_key(key), m_value(value) { 
			PrepareNames();
		}

		virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
			//PrepareNames(); 
			return &m_methodHelper;
		}
		virtual void PrepareNames() const;

		virtual bool SetPropVal(const long lPropNum, CValue& cValue);        //установка атрибута
		virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута
	};

public:

	CValueContainer();
	CValueContainer(const std::map<CValue, CValue>& containerValues);
	CValueContainer(bool readOnly);

	virtual ~CValueContainer();

	virtual bool SetPropVal(const long lPropNum, const CValue& cValue);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;                         //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);       //вызов метода

	//–асширенные методы:
	virtual void Insert(const CValue& varKeyValue, const CValue& cValue);
	virtual void Delete(const CValue& varKeyValue);
	virtual bool Property(const CValue& varKeyValue, CValue& cValueFound);
	unsigned int Count() const { return m_containerValues.size(); }
	void Clear() { m_containerValues.clear(); }

	//–абота с итераторами:
	virtual bool HasIterator() const { return true; }
	virtual CValue GetIteratorEmpty();
	virtual CValue GetIteratorAt(unsigned int idx);
	virtual unsigned int GetIteratorCount() const { return m_containerValues.size(); }

protected:

	CMethodHelper* m_methodHelper;

	struct ContainerComparator {
		bool operator()(const CValue& lhs, const CValue& rhs) const;
	};

	std::map<const CValue, CValue, ContainerComparator> m_containerValues;
};

// structure  
class BACKEND_API CValueStructure : public CValueContainer {
	wxDECLARE_DYNAMIC_CLASS(CValueStructure);
public:

	CValueStructure() : CValueContainer(false) {}
	CValueStructure(const std::map<wxString, CValue>& structureValues) : CValueContainer(true) {
		for (auto& strBVal : structureValues) m_containerValues.insert_or_assign(strBVal.first, strBVal.second); 
		PrepareNames();
	}

	CValueStructure(bool readOnly) : CValueContainer(readOnly) {}

	virtual void Delete(const CValue& varKeyValue) override;
	virtual void Insert(const CValue& varKeyValue, const CValue& cValue = CValue()) override;
	virtual bool Property(const CValue& varKeyValue, CValue& cValueFound) override;

	virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);
	virtual bool SetAt(const CValue& varKeyValue, const CValue& cValue);
};

#endif