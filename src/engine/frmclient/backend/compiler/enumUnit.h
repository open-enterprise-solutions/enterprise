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

	wxString GetEnumName(unsigned int idx) const {
		if (idx >= m_listEnumData.size())
			return wxEmptyString;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->second;
	}

	wxString GetEnumDesc(unsigned int idx) const {
		if (idx >= m_listEnumDesc.size())
			return wxEmptyString;
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

	inline void AddEnumeration(const valT& v, const wxString& name, const wxString& descr = wxEmptyString) {
		m_listEnumData.emplace(v, name);
		m_listEnumDesc.emplace(v, descr.IsEmpty() ? name : descr);
	}

private:

	std::map<valT, wxString> m_listEnumData;
	std::map<valT, wxString> m_listEnumDesc;
	valT                     m_value = valT();
};

// A MEMBER OF AN ENUMERATION — the engine's ibValueEnumerationVariant: it reads as its word, is its number, is written as
// its enumeration's class and that number, and offers its siblings to a quick choice (FindValue).
template <typename valT>
class ibValueEnumerationVariant : public ibValue {
public:

	using ibMembers = std::vector<std::pair<valT, wxString>>;

	ibValueEnumerationVariant(const ibClassID& enumClass, const valT& v, const wxString& word,
		std::shared_ptr<const ibMembers> members)
		: ibValue(enumClass, static_cast<long>(v), word), m_members(std::move(members)) {}

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& foundedObjects) const override {
		if (m_members == nullptr)
			return false;
		for (const auto& [member, word] : *m_members) {
			if (findData.IsEmpty() || word.Lower().StartsWith(findData.Lower()))
				foundedObjects.push_back(ibValue(new ibValueEnumerationVariant(GetClassType(), member, word, m_members)));
		}
		return true;
	}

private:

	std::shared_ptr<const ibMembers> m_members;
};

template <typename valT>
ibValue ibValueEnumeration<valT>::CreateEnumVariantValue(const valT& v) const {
	auto members = std::make_shared<typename ibValueEnumerationVariant<valT>::ibMembers>();
	for (const auto& [member, word] : m_listEnumDesc)
		members->emplace_back(member, word);
	const auto found = m_listEnumDesc.find(v);
	return ibValue(new ibValueEnumerationVariant<valT>(GetClassType(), v,
		found != m_listEnumDesc.end() ? found->second : wxString(), members));
}

template <class T, typename valT>
ibValue ibValue::CreateEnumObject(const valT& v) {
	T enumeration;
	enumeration.Init();
	return enumeration.CreateEnumVariantValue(v);
}

#endif
