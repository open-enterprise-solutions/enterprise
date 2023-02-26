#ifndef _VALUEARRAY_H__
#define _VALUEARRAY_H__

#include "value.h"

//Поддержка массивов
class CValueArray : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CValueArray);
private:
	std::vector <CValue> m_arrayValues;
private:

	enum Func {
		enAdd = 0,
		enInsert,
		enCount,
		enFind,
		enClear,
		enGet,
		enSet,
		enRemove
	};

	inline void CheckIndex(unsigned int index) const;

public:

	CValueArray() :
		CValue(eValueTypes::TYPE_VALUE) {
	}

	CValueArray(const std::vector <CValue>& arr) :
		CValue(eValueTypes::TYPE_VALUE, true), m_arrayValues(arr) {
	}

	virtual ~CValueArray() {
		Clear();
	}

	virtual bool Init(CValue** paParams, const long lSizeArray);

	virtual wxString GetTypeString() const {
		return wxT("array");
	}

	virtual wxString GetString() const {
		return wxT("array");
	}

	//check is empty
	virtual inline bool IsEmpty() const override {
		return m_arrayValues.empty();
	}

public:

	//Атрибут -> Строковый ключ
	//работа с массивом как с агрегатным объектом
	static CMethodHelper m_methodHelper;

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		return &m_methodHelper;
	}
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);       //вызов метода

	//Расширенные методы
	void Add(const CValue& varValue) {
		m_arrayValues.push_back(varValue);
	}

	void Insert(unsigned int index, const CValue& varValue) {
		CheckIndex(index);
		m_arrayValues.insert(m_arrayValues.begin() + index, varValue);
	}

	unsigned int Count() const {
		return m_arrayValues.size();
	}

	CValue Find(const CValue& varValue) {
		auto foundedIt = std::find(m_arrayValues.begin(), m_arrayValues.end(), varValue);
		if (foundedIt != m_arrayValues.end())
			return std::distance(m_arrayValues.begin(), foundedIt);
		return CValue();
	}

	void Remove(unsigned int index) {
		CheckIndex(index);
		auto foundedIt = std::find(m_arrayValues.begin(), m_arrayValues.end(), index);
		if (foundedIt != m_arrayValues.end())
			m_arrayValues.erase(foundedIt);
	}

	void Clear() {
		m_arrayValues.clear();
	}

	//array support 
	virtual bool SetAt(const CValue& varKeyValue, const CValue& varValue);
	virtual bool GetAt(const CValue& varKeyValue, CValue& pvarValue);

	//Работа с итераторами 
	virtual bool HasIterator() const {
		return true;
	}
	virtual CValue GetItAt(unsigned int idx) {
		return m_arrayValues[idx];
	}
	virtual unsigned int GetItSize() const {
		return Count();
	}
};

#endif