#ifndef _FRMCLIENT_BACKEND_ENUM_UNIT_H__
#define _FRMCLIENT_BACKEND_ENUM_UNIT_H__

#include <map>

#include "frmclient/backend/compiler/value.h"

// AN ENUMERATION — the engine's (backend/compiler/enumUnit.h) as a property of the client reads its choices from it:
// each member's number, its name and its caption, in the order of the numbers, added by the enumeration itself
// (CreateEnumeration) once it is made (Init). A script's value of one is the server's.
template <typename valT>
class ibValueEnumeration : public ibValue {
public:

	using valEnumType = valT;

	ibValueEnumeration() = default;

	virtual bool Init() override {
		if (m_listEnumData.empty())
			CreateEnumeration();
		return true;
	}
	bool Init(ibValue** paParams, const long lSizeArray) {
		Init();
		m_value = lSizeArray > 0 && paParams[0] != nullptr ? paParams[0]->ConvertToEnumValue<valT>() : valT();
		return true;
	}

	// The member chosen, as a value — its number.
	ibValue GetEnumVariantValue() const { return ibValue(static_cast<long>(m_value)); }

	ibString GetEnumName(unsigned int idx) const {
		if (idx >= m_listEnumData.size())
			return ibEmptyString;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->second;
	}

	ibString GetEnumDesc(unsigned int idx) const {
		if (idx >= m_listEnumDesc.size())
			return ibEmptyString;
		auto it = m_listEnumDesc.begin();
		std::advance(it, idx);
		return it->second;
	}

	valT GetEnumValue(unsigned int idx) const {
		if (idx >= m_listEnumData.size())
			return (valT)0;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->first;
	}

	unsigned int GetEnumCount() const { return m_listEnumData.size(); }

	// A member as a value — the engine's: its word as it reads (the description), its number, its enumeration's class —
	// and its siblings, which a quick choice offers it among.
	ibValue CreateEnumVariantValue(const valT& v) const;

protected:

	//create enumeration
	virtual void CreateEnumeration() = 0;

	inline void AddEnumeration(const valT& v, const ibString& name, const ibString& descr = ibEmptyString) {
		m_listEnumData.emplace(v, name);
		m_listEnumDesc.emplace(v, descr.IsEmpty() ? name : descr);
	}

private:

	std::map<valT, ibString> m_listEnumData;
	std::map<valT, ibString> m_listEnumDesc;
	valT                     m_value = valT();
};

// A MEMBER OF AN ENUMERATION — the engine's ibValueEnumerationVariant: it reads as its word, is its number, is written as
// its enumeration's class and that number, and offers its siblings to a quick choice (FindValue).
template <typename valT>
class ibValueEnumerationVariant : public ibValue {
public:

	using ibMembers = std::vector<std::pair<valT, ibString>>;

	ibValueEnumerationVariant(const ibClassID& enumClass, const valT& v, const ibString& word,
		std::shared_ptr<const ibMembers> members)
		: ibValue(ibValueTypes::TYPE_ENUM), m_classType(enumClass), m_member(v), m_word(word), m_members(std::move(members)) {}

	virtual ibClassID GetClassType() const override { return m_classType; }
	virtual ibString GetString() const override { return m_word; }
	virtual ibNumber GetNumber() const override { return ibNumber(static_cast<long>(m_member)); }
	// The negative range is "no member" — the engine's.
	virtual bool IsEmpty() const override { return static_cast<long>(m_member) < 0; }

	// The same member of the same enumeration.
	virtual bool CompareValueEQ(const ibVariant& value) const override {
		return value.GetType() == ibValueTypes::TYPE_ENUM && value.GetClassType() == m_classType
			&& value.GetInteger() == static_cast<long>(m_member);
	}

	virtual bool FindValue(const ibString& findData, std::vector<ibValue>& foundedObjects) const override {
		if (m_members == nullptr)
			return false;
		for (const auto& [member, word] : *m_members) {
			if (findData.IsEmpty() || word.Lower().StartsWith(findData.Lower()))
				foundedObjects.push_back(new ibValueEnumerationVariant(GetClassType(), member, word, m_members));
		}
		return true;
	}

protected:

	// Written as its enumeration's class (Serialize) and the member, by its number.
	virtual bool DoSerialize(ibDataNode& node) const override {
		node.SetValue(kValueFieldData, static_cast<std::int32_t>(m_member));
		return true;
	}

private:

	ibClassID                        m_classType;
	valT                             m_member;
	ibString                         m_word;
	std::shared_ptr<const ibMembers> m_members;
};

template <typename valT>
ibValue ibValueEnumeration<valT>::CreateEnumVariantValue(const valT& v) const {
	auto members = std::make_shared<typename ibValueEnumerationVariant<valT>::ibMembers>();
	for (const auto& [member, word] : m_listEnumDesc)
		members->emplace_back(member, word);
	const auto found = m_listEnumDesc.find(v);
	return ibValue(new ibValueEnumerationVariant<valT>(GetClassType(), v,
		found != m_listEnumDesc.end() ? found->second : ibString(), members));
}

template <class T, typename valT>
ibValue ibValue::CreateEnumObject(const valT& v) {
	T enumeration;
	enumeration.Init();
	return enumeration.CreateEnumVariantValue(v);
}

#endif
