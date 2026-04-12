#ifndef __VALUE_MAP_H__
#define __VALUE_MAP_H__

#include "backend/compiler/value.h"

class BACKEND_API ibValueContainer : public ibValue {
	wxDECLARE_DYNAMIC_CLASS(ibValueContainer);
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
	virtual bool HasIterator() const { return true; }
	virtual ibValue GetIteratorEmpty();
	virtual ibValue GetIteratorAt(unsigned int idx);
	virtual unsigned int GetIteratorCount() const { return m_containerValues.size(); }

protected:

	ibValueMethodHelper* m_methodHelper;

	struct ContainerComparator {
		bool operator()(const ibValue& lhs, const ibValue& rhs) const;
	};

	std::map<const ibValue, ibValue, ContainerComparator> m_containerValues;
};

// structure  
class BACKEND_API ibValueStructure : public ibValueContainer {
	wxDECLARE_DYNAMIC_CLASS(ibValueStructure);
public:

	ibValueStructure() : ibValueContainer(false) {}
	ibValueStructure(const std::map<wxString, ibValue>& structureValues) : ibValueContainer(true) {
		for (auto& strBVal : structureValues) m_containerValues.insert_or_assign(strBVal.first, strBVal.second); 
		PrepareNames();
	}

	ibValueStructure(bool readOnly) : ibValueContainer(readOnly) {}

	virtual void Delete(const ibValue& varKeyValue) override;
	virtual void Insert(const ibValue& varKeyValue, const ibValue& cValue = ibValue()) override;
	virtual bool Property(const ibValue& varKeyValue, ibValue& cValueFound) override;

	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);
	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& cValue);
};

#include <locale>

#endif