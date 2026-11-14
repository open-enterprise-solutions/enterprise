#ifndef __ENUM_UNIT_H__
#define __ENUM_UNIT_H__

#include "value.h"
#include "core/exception.h"               // ibCoreException - a number no member has is refused in words
#include "core/serialize/dataBuilder.h"   // ibDataNode - the enum writes its member into one

#include <memory>

// ⭐⭐ WHAT THE MEMBERS ARE BELONGS TO THE TYPE, NOT TO EACH VALUE (2026-11-14). Every value built its own: the names,
// two maps and the names as properties, all of them again — for each cell of an enum column read, for each
// `ComparisonKind.Equal` written — translating every description on the way, in whatever language the thread had.
// The type's list is built once (ibCtorEnumType::CreateObject) and every value, and every member it hands out,
// holds it.
class BACKEND_API ibValueEnumerationWrapper : public ibValue {
public:

	// The type's part that does not depend on the members' number type: their names in the order they are declared
	// (the property number counts in that order, see GetPropVal) and the same names as properties.
	class ibEnumerationList {
	public:
		std::vector<ibString> m_listEnumStr;
		ibMemberTable         m_members;
	};

	ibValueEnumerationWrapper(bool createInstance = false);
	virtual ~ibValueEnumerationWrapper();

	virtual ibValue* GetEnumVariantValue() const = 0;
	virtual ibString GetClassName() const = 0;
	virtual ibString GetString() const = 0;
};

//***************************************************************************************************
//*                                 Current variant from ibValueEnumerationBase                     *
//***************************************************************************************************

template <typename valT>
class ibValueEnumerationVariantBase : public ibValue {
	public:

	ibValueEnumerationVariantBase() : ibValue(ibValueTypes::TYPE_ENUM, true) {}

	virtual valT GetEnumValue() const = 0;
	virtual void SetEnumValue(const valT& v) = 0;

	// PACKING AN ENUM IS PACKING ITS MEMBER — the header already carries the type, so the member
	// number is the whole of the contents. Without this the base's switch fell through to "a type
	// with contents of its own that did not override this" and answered NO, which means an
	// enumeration could not be stored ANYWHERE: a saved list filter on an enum column came back
	// empty (the copy is made by packing, and a refusal left the buffer cleared), and so did every
	// other setting that happened to hold one. It read as "the filter will not display".
	virtual bool DoSerialize(class ibDataNode& node) const override {
		node.SetValue(kValueFieldData, (std::int32_t)GetEnumValue());
		return true;
	}
	virtual bool DoDeserialize(const class ibDataNode& node) override {
		SetEnumValue(static_cast<valT>(node.GetValue<std::int32_t>(kValueFieldData)));
		return true;
	}
};

//***************************************************************************************************
//*                                       Base collection variant                                   *
//***************************************************************************************************

template <typename valT>
class ibValueEnumerationBase : public ibValueEnumerationWrapper {
	public:

	ibValueEnumerationBase(bool createInstance = false) :
		ibValueEnumerationWrapper(createInstance)
	{
	}

	virtual bool Init() { return true; }

	virtual bool Init(ibValue** paParams, const long lSizeArray) {
		if (lSizeArray < 1)
			return false;
		SetEnumValue(
			static_cast<valT>(paParams[0]->GetInteger())
		);
		return true;
	}

	virtual valT GetEnumValue() const = 0;
	virtual void SetEnumValue(const valT& v) = 0;
};

//default base class for all enumerations
template <typename valT>
class ibValueEnumeration : public ibValueEnumerationBase<valT> {
public:

	// …and what depends on it: each member's name and description by its number. The description is kept as the
	// catalog keeps it — a msgid, with its context where it has one — and translated where it is read, in the
	// reader's language: one list serves every session.
	class ibEnumerationList : public ibValueEnumerationWrapper::ibEnumerationList {
	public:
		// What a person reads for a member: its description, translated; its name where it was declared without
		// one; nothing for a number no member has (a member read back as "none" holds one).
		ibString GetEnumDescription(const valT& v) const {
			const auto name = m_listEnumData.find(v);
			if (name == m_listEnumData.end())
				return ibString();
			const ibString& description = m_listEnumDesc.at(v);
			if (description.IsEmpty())
				return name->second;
			const auto context = m_listEnumContext.find(v);
			return ibGetTranslation(description, context != m_listEnumContext.end() ? context->second : ibString());
		}

		std::map<valT, ibString> m_listEnumData, m_listEnumDesc, m_listEnumContext;
	};

protected:

	template <typename valType>
	class ibValueEnumerationVariant : public ibValueEnumerationVariantBase<valType> {
	public:

		ibValueEnumerationVariant(const valType& v, const ibClassID& clsid, const std::shared_ptr<ibEnumerationList>& list) :
			ibValueEnumerationVariantBase<valType>(), m_clsid(clsid), m_value(v), m_list(list) {}

		virtual valT GetEnumValue() const override { return m_value; }
		virtual void SetEnumValue(const valT& v) override { m_value = v; }

		// A member of a built-in enumeration has no fields for a condition to narrow by.
		virtual bool FindValue(const ibString& input, const ibChoiceCondition& /*condition*/, std::vector<ibValue>& out) const override {
			for (const auto& e : m_list->m_listEnumData) {
				if (e.second.Contains(input))
					out.push_back(new ibValueEnumerationVariant<valType>(e.first, m_clsid, m_list));
			}
			std::sort(out.begin(), out.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
			return out.size() > 0;
		}

		//operator '=='
		virtual bool CompareValueEQ(const ibVariant& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value == compareEnumeration->m_value;
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value == compareEnumerationOwner->GetEnumValue();
			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibVariant& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value != compareEnumeration->m_value;
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value != compareEnumerationOwner->GetEnumValue();
			return true;
		}

		// ⭐⭐ ORDERED BY THE MEMBER, NOT BY ITS TEXT (Max, 2026-08-29). Sorting a column of an enumeration
		// must follow the order the enumeration DECLARES — "Wholesale, Retail" is the author's sequence and
		// it carries meaning, while the alphabet of the presentations is an accident of the words and moves
		// the moment they are translated.
		//
		// 🛑 Only the ORDER was still text. The `==` family above has always answered by the member, so a
		// filter on an enum column agreed with the data while a SORT of the same column did not — one value,
		// two orders. `<` is the primitive the rest of the family is stated over (see value.h), so this one
		// override turns `>`, `<=` and `>=` with it.
		//
		// ⚠ NOTHING CHOSEN SORTS FIRST, and by the same rule rather than by an exception: the empty
		// enumeration is written in the NEGATIVE range (see IsEmpty below), so it simply is the smallest
		// member. A cell holding no enumeration AT ALL is not this — it is TYPE_EMPTY and the base answers
		// for it, also first.
		//
		// A comparand that is not an enumeration of this kind is left to the base: what orders a mixed
		// column is its kind ranking, and this override has nothing truer to say about it.
		virtual int CompareValueLS(const ibVariant& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration)
				return m_value < compareEnumeration->m_value ? -1 : (compareEnumeration->m_value < m_value ? 1 : 0);
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) {
				const valType there = compareEnumerationOwner->GetEnumValue();
				return m_value < there ? -1 : (there < m_value ? 1 : 0);
			}
			return ibValue::CompareValueLS(cParam);
		}

		//get type id
		virtual ibClassID GetClassType() const override { return m_clsid; }

		// ⭐ BY THE SIGN, not by a member: a member number is non-negative whatever the declaration
		// chose (backend_core.h), and the negative range is what "nothing chosen" is written in — the
		// same number the write binds into a column carrying no value and the DDL defaults it to. So
		// a variant built from it is the empty enumeration in its OTHER shape: same state, different
		// carrier. Answering "not empty" here let it walk past every fill-check the owner now stops.
		virtual bool IsEmpty() const override { return static_cast<long>(m_value) <= emptyEnum; }

		//type info
		virtual ibString GetClassName() const override { return ibValue::GetNameObjectFromID(m_clsid); }

		//type conversion
		// THE DESCRIPTION IS WHAT A PERSON READS — the caption in their own
		// language, translated, not the identifier behind it. The NAME is
		// the identifier a script writes (`ComparisonKind.Equal`) and what a saved
		// setting round-trips; presenting it in a picker or a cell makes the form
		// speak in identifiers. Falls back to the name when a member was declared
		// without a description, so nothing is ever blank.
		virtual ibString GetString() const override {
			return m_list->GetEnumDescription(m_value);
		}
		// ⚠ CAST, NOT AN IMPLICIT CONVERSION — the same one IsEmpty above already writes. A member
		// number is a number, but only an UNSCOPED enum says so on its own: a scoped one
		// (`enum class`) converts to nothing implicitly, so this line was what refused to register a
		// scoped enumeration in the runtime at all — and refused it in the template, where the
		// enumeration itself is fine.
		virtual ibNumber GetNumber() const override { return static_cast<long>(m_value); }

	private:
		// ⭐ THE DEFAULT STATE IS "NO MEMBER", SPELLED — not whatever the memory happened to hold.
		// Every member number is valid, so an uninitialised field is indistinguishable from a
		// choice, and the one thing it can never be is caught. Starting at emptyEnum means the
		// only way to hold a member is to have been given one.
		ibClassID m_clsid = 0;
		valType m_value = static_cast<valType>(emptyEnum);
		std::shared_ptr<ibEnumerationList> m_list;   // the type's: what the member number is called and reads as
	};

public:

	using valEnumType = valT;

	// A member, declared — by the type's CreateEnumeration, into the type's list. The description is a msgid
	// (ibTRANSLATE), and `context` its catalog context where it has one (ibTRANSLATE_IN_CONTEXT).
	inline void AddEnumeration(const valT& v, const ibString& name, const ibString& descr = ibEmptyString, const ibString& context = ibEmptyString) {
		if (m_list == nullptr)
			m_list = std::make_shared<ibEnumerationList>();
		ibASSERT(!m_list->m_listEnumData.contains(v));
		m_list->m_listEnumData.insert_or_assign(v, name);
		m_list->m_listEnumDesc.insert_or_assign(v, descr);
		if (!context.IsEmpty())
			m_list->m_listEnumContext.insert_or_assign(v, context);

		m_list->m_listEnumStr.push_back(name);
		m_list->m_members.AppendProp(name);
	}

	ibValueEnumeration() : ibValueEnumerationBase<valT>(true) {}
	virtual ~ibValueEnumeration() {}

	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override {
		if (lSizeArray < 1)
			return false;
		const valT& defValue = static_cast<valT>(paParams[0]->GetInteger());
		ibValueEnumeration::InitializeEnumeration(defValue);
		return true;
	}

	virtual valT GetEnumValue() const override {
		if (m_value != nullptr) {
			return m_value->GetEnumValue();
		}
		// NOT valT() — that is ZERO, and zero is an ordinary member number. An enumeration with no
		// member answered as though that member had been chosen, the same mistake the column default
		// made one layer up.
		return static_cast<valT>(emptyEnum);
	}

	// ⭐ SETTING A MEMBER ON AN ENUMERATION THAT HAS NONE MUST CREATE IT. The guard read as "keep the
	// carrier in step", but an owner with no member yet is exactly the state a value arrives in —
	// from AdjustValue, from a cleared field — so the assignment it was meant to protect was the one
	// that mattered, and it did nothing at all, silently. The field stayed blank and the write
	// carried nothing.
	// …AND ON ONE THAT HAS A MEMBER IT IS MADE ANEW, by the same door: a member is the type's list and a number, so a
	// new one costs what changing the old one did, the door refuses a number no member has, and a member handed out
	// before keeps the one it had.
	virtual void SetEnumValue(const valT& v) override {
		InitializeEnumeration(v);
	}

	// ⭐⭐ AN ENUMERATION WITH NO MEMBER STILL HAS A PACKED FORM — "no member".
	//
	// The VARIANT got these long ago (see the note on ibValueEnumerationVariantBase), and the reason
	// given there applies to this shape word for word: without them the base's switch falls through
	// to "a type with contents of its own that did not override this" and answers NO — which is not
	// "empty", it is a REFUSAL, and the refusal surfaces as "Failed to read the contents of a value
	// of type 'AccountType'" the moment anything holding one is read back. A list filter over an
	// enum column holds exactly that: the row is created with the column's empty value, and the
	// dialog could not be opened again afterwards.
	//
	// emptyEnum is the member number that means "none" — the same one the enum column binds when the
	// cell carries no value, so the packed form and the stored column agree.
	virtual bool DoSerialize(class ibDataNode& node) const override {
		node.SetValue(kValueFieldData, m_value != nullptr ? (std::int32_t)m_value->GetEnumValue() : (std::int32_t)emptyEnum);
		return true;
	}

	virtual bool DoDeserialize(const class ibDataNode& node) override {
		const std::int32_t member = node.GetValue<std::int32_t>(kValueFieldData);
		if (member <= emptyEnum) {
			m_value = decltype(m_value)();   // read back as written: an enumeration with no member
			return true;
		}
		InitializeEnumeration(static_cast<valT>(member));
		return true;
	}

	//create enumeration — the type's members, once (ibCtorEnumType::CreateObject)
	virtual void CreateEnumeration() = 0;

	// ⚠ A NUMBER NO MEMBER HAS IS REFUSED — it always was, by the lookup of its name (std::map::at); now in words.
	// A reader that may hold one ("none", read from a column) answers empty in its catch, not with a member of nothing.
	ibValueEnumerationVariant<valT>* CreateEnumVariantValue(const valT& v) const {
		if (!m_list->m_listEnumData.contains(v))
			ibCoreException::Error(_("'%s' has no member numbered %d"), ibValue::GetClassName(), static_cast<int>(v));
		return new ibValueEnumerationVariant<valT>(v, ibValue::GetClassType(), m_list);
	}

	// The type's list taken by a value — the type built it once (ibCtorEnumType::CreateObject).
	void InitializeEnumeration(const ibValueEnumeration& type) {
		m_list = type.m_list;
	}

	void InitializeEnumeration(const valT& v) {
		m_value = CreateEnumVariantValue(v);
	}

	virtual ibValue* GetEnumVariantValue() const override { return m_value; }

	// ⚠ `>=`, NOT `>`: a position equal to the count is past the last member, and stepping the map there is undefined.
	ibString GetEnumName(unsigned int idx) const {
		if (idx >= m_list->m_listEnumData.size())
			return ibEmptyString;
		auto it = m_list->m_listEnumData.begin();
		std::advance(it, idx);
		return it->second;
	}

	ibString GetEnumDesc(unsigned int idx) const {
		if (idx >= m_list->m_listEnumData.size())
			return ibEmptyString;
		auto it = m_list->m_listEnumData.begin();
		std::advance(it, idx);
		return m_list->GetEnumDescription(it->first);
	}

	valT GetEnumValue(unsigned int idx) const {
		if (idx >= m_list->m_listEnumData.size())
			return (valT)0;
		auto it = m_list->m_listEnumData.begin();
		std::advance(it, idx);
		return it->first;
	}

	// A NUMBER — a property's, read from a file — asked before it is taken for a member, and never by casting it: a
	// number no member has is no value of an enumeration without a fixed type, and the cast is undefined (UBSan stopped
	// on version 99 read as ibProgramVersion).
	bool HasEnumValue(long v) const {
		for (const auto& e : m_list->m_listEnumData) {
			if (static_cast<long>(e.first) == v)
				return true;
		}
		return false;
	}

	unsigned int GetEnumCount() const { return m_list->m_listEnumData.size(); }

	// ⚠ THE PROPERTY NUMBER COUNTS IN DECLARATION ORDER (the names as listed, m_listEnumStr), and the
	// member table is a map ordered by VALUE. Stepping through the map by that number agreed only while
	// an enumeration happened to be declared in ascending order: `Chars` is not (CR = 13 first, Tab = 9
	// fifth), so `Chars.LF` came back as VTab (measured 2026-09-10). The name at that position is the
	// one fact both orders share.
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override { //attribute value
		if (lPropNum < 0 || static_cast<size_t>(lPropNum) >= m_list->m_listEnumStr.size())
			return false;
		const ibString& name = m_list->m_listEnumStr[lPropNum];
		for (const auto& e : m_list->m_listEnumData) {
			if (e.second != name)
				continue;
			pvarPropVal = CreateEnumVariantValue(e.first);
			return true;
		}
		return false;
	}

	// A member of a built-in enumeration has no fields for a condition to narrow by.
	virtual bool FindValue(const ibString& input, const ibChoiceCondition& /*condition*/, std::vector<ibValue>& out) const override {
		for (const auto& e : m_list->m_listEnumData) {
			if (e.second.Contains(input))
				out.push_back(new ibValueEnumerationVariant<valT>(e.first, ibValue::GetClassType(), m_list));
		}
		std::sort(out.begin(), out.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
		return out.size() > 0;
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibVariant& cParam) const override {
		if (m_value != nullptr) {
			return m_value->CompareValueEQ(cParam);
		}
		return ibValue::CompareValueEQ(cParam);
	}

	//operator '!='
	virtual bool CompareValueNE(const ibVariant& cParam) const override {
		if (m_value != nullptr) {
			return m_value->CompareValueNE(cParam);
		}
		return ibValue::CompareValueNE(cParam);
	}

	// ⭐ NO MEMBER CHOSEN IS EMPTY. This answered NO unconditionally, while the two methods right
	// below already tell the truth about the same state — GetString returns "" and GetNumber returns
	// ibNOT_FOUND when m_value is null. Fill-check asks exactly this question (SaveData), so a
	// required enumeration left unset was saved as if it had been filled in, and the refusal the
	// attribute's flag promised never came. The VARIANT stays non-empty: a chosen member is a value.
	virtual bool IsEmpty() const override { return m_value == nullptr; }

	//type info
	virtual ibString GetClassName() const final {
		return m_value ? m_value->GetClassName() :
			ibValue::GetClassName();
	};

	//type conversion
	virtual ibString GetString() const final {
		return m_value ? m_value->GetString() :
			ibString();
	}

	virtual ibNumber GetNumber() const final {
		return m_value ? m_value->GetNumber() :
			emptyEnum;
	}

protected:

	// The members' names as properties are the type's — its list's. The type's own value, while its CreateEnumeration
	// has not filled the list yet, has none.
	virtual ibValue::ibMemberTable* DoGetPMethods() const override { return m_list != nullptr ? &m_list->m_members : nullptr; }

	ibValuePtr<ibValueEnumerationVariant<valT>> m_value; //current enum value
	std::shared_ptr<ibEnumerationList> m_list;            // the type's members — built once, held by every value
};

#endif