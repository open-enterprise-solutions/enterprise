#ifndef __VALUE_MAP_H__
#define __VALUE_MAP_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueContainer : public ibValue {
	public:
private:
	enum Func  {
		enCount = 0,
		enProperty,
		enClear,
		enDelete,
		enInsert
	};
public:

	//Attribute -> String key
	//working with an array as an aggregate object:

	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);
	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& cValue);

	//check is empty
	virtual bool IsEmpty() const { 
		return m_containerValues.empty();
	}

public:

	class BACKEND_API ibValueReturnContainer : public ibValue {
	public:
		
		enum Prop {
			enKey,
			enValue
		};
		
		ibValue m_key;
		ibValue m_value;
	
		static ibValueMethodHelper m_methodHelper;

	public:

		ibValueReturnContainer() : ibValue(ibValueTypes::TYPE_VALUE, true) { 
			PrepareNames(); 
		}
		
		ibValueReturnContainer(const ibValue& key, ibValue& value) : ibValue(ibValueTypes::TYPE_VALUE, true), m_key(key), m_value(value) { 
			PrepareNames();
		}

		virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
			//PrepareNames(); 
			return &m_methodHelper;
		}
		virtual void PrepareNames() const;

		virtual bool SetPropVal(const long lPropNum, ibValue& cValue);        //setting attribute
		virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	};

public:

	ibValueContainer();
	ibValueContainer(const std::map<ibValue, ibValue>& containerValues);
	ibValueContainer(bool readOnly);

	virtual ~ibValueContainer();

	virtual bool SetPropVal(const long lPropNum, const ibValue& cValue);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //method call

	//Расширенные методы:
	virtual void Insert(const ibValue& varKeyValue, const ibValue& cValue);
	virtual void Delete(const ibValue& varKeyValue);
	virtual bool Property(const ibValue& varKeyValue, ibValue& cValueFound);
	unsigned int Count() const { return m_containerValues.size(); }
	void Clear() { m_containerValues.clear(); }

	//Работа с итераторами:
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

protected:

	ibValueMethodHelper* m_methodHelper;

	struct ContainerComparator {
		bool operator()(const ibValue& lhs, const ibValue& rhs) const;
	};

	std::map<const ibValue, ibValue, ContainerComparator> m_containerValues;
};

// structure  
class BACKEND_API ibValueStructure : public ibValueContainer {
	public:

	ibValueStructure() : ibValueContainer(false) {}
	ibValueStructure(const std::map<wxString, ibValue>& structureValues) : ibValueContainer(true) {
		for (auto& strBVal : structureValues) m_containerValues.insert_or_assign(strBVal.first, strBVal.second); 
		PrepareNames();
	}

	ibValueStructure(bool readOnly) : ibValueContainer(readOnly) {}

	// `New Structure("Field1, Field2, ...", value1, value2, ...)` —
	// 1C-style ctor: first arg is comma-separated field-name list,
	// subsequent args are corresponding values (missing values default
	// to TYPE_EMPTY). No-arg form `New Structure` produces an empty
	// structure to be populated via Insert(name, value) later.
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual void Delete(const ibValue& varKeyValue) override;
	virtual void Insert(const ibValue& varKeyValue, const ibValue& cValue = ibValue()) override;
	virtual bool Property(const ibValue& varKeyValue, ibValue& cValueFound) override;

	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);
	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& cValue);
};

#include <locale>

#endif