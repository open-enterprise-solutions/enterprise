#ifndef _VALUEMAP_H__
#define _VALUEMAP_H__

#include "value.h"
#include <locale>

class CValueContainer : public CValue
{
	wxDECLARE_DYNAMIC_CLASS(CValueContainer);

public:

	//јтрибут -> —троковый ключ
	//работа с массивом как с агрегатным объектом:

	virtual CValue GetAt(const CValue& cKey);
	virtual void SetAt(const CValue& cKey, CValue& cValue);

	virtual wxString GetTypeString() const { return wxT("container"); }
	virtual wxString GetString() const { return wxT("container"); }

	//check is empty
	virtual inline bool IsEmpty() const override { return m_containerValues.empty(); }

public:

	class CValueReturnContainer : public CValue
	{
		CValue m_key;
		CValue m_value;

		static CMethods m_methods;

	public:

		CValueReturnContainer() : CValue(eValueTypes::TYPE_VALUE, true) { PrepareNames(); }
		CValueReturnContainer(const CValue& key, CValue& value) : CValue(eValueTypes::TYPE_VALUE, true), m_key(key), m_value(value) { PrepareNames(); }

		virtual CMethods* GetPMethods() const { return &m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
		virtual void PrepareNames() const;

		virtual void SetAttribute(attributeArg_t& aParams, CValue& cValue);        //установка атрибута
		virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

		virtual wxString GetTypeString() const { return wxT("keyValue"); }
		virtual wxString GetString() const { return wxT("keyValue"); }
	};

public:

	CValueContainer();
	CValueContainer(const std::map<CValue, CValue>& containerValues);
	CValueContainer(bool readOnly);

	~CValueContainer();

	static CMethods m_methods;

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cValue);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	virtual CMethods* GetPMethods() const { PrepareNames();  return &m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызываетс€ дл€ инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);       //вызов метода

	//–асширенные методы:
	virtual void Insert(const CValue& cKey, CValue& cValue);
	virtual void Delete(const CValue& cKey);
	virtual bool Property(const CValue& cKey, CValue& cValueFound);
	unsigned int Count() const { return m_containerValues.size(); }
	void Clear() { m_containerValues.clear(); }

	//–абота с итераторами:
	virtual bool HasIterator() const { return true; }
	virtual CValue GetItEmpty();
	virtual CValue GetItAt(unsigned int idx);
	virtual unsigned int GetItSize() const { return m_containerValues.size(); }

protected:

	struct ContainerComparator
	{
		bool operator()(const CValue& lhs, const CValue& rhs) const;
	};

	std::map<const CValue, CValue, ContainerComparator> m_containerValues;
};

// structure  
class CValueStructure : public CValueContainer
{
	wxDECLARE_DYNAMIC_CLASS(CValueStructure);

public:

	CValueStructure() : CValueContainer(false) {}
	CValueStructure(const std::map<wxString, CValue>& structureValues) : CValueContainer(true) { for (auto& strBVal : structureValues) m_containerValues.insert_or_assign(strBVal.first, strBVal.second); }

	CValueStructure(bool readOnly) : CValueContainer(readOnly) {}

	virtual void Delete(const CValue& cKey) override;
	virtual void Insert(const CValue& cKey, CValue& cValue = CValue()) override;
	virtual bool Property(const CValue& cKey, CValue& cValueFound) override;

	virtual CValue GetAt(const CValue& cKey);
	virtual void SetAt(const CValue& cKey, CValue& cValue);

	virtual wxString GetTypeString() const { return wxT("structure"); }
	virtual wxString GetString() const { return wxT("structure"); }
};

#endif