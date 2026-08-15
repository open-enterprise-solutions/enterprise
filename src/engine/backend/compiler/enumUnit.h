#ifndef __ENUM_UNIT_H__
#define __ENUM_UNIT_H__

#include "value.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode - the enum writes its member into one

class BACKEND_API ibValueEnumerationWrapper : public ibValueDynamicMembers {
public:

	ibValueEnumerationWrapper(bool createInstance = false);
	virtual ~ibValueEnumerationWrapper();

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual ibValue* GetEnumVariantValue() const = 0;
	virtual wxString GetClassName() const = 0;
	virtual wxString GetString() const = 0;

protected:
	std::vector<wxString> m_listEnumStr;
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
		node.SetValue(kValueFieldData, (s32)GetEnumValue());
		return true;
	}
	virtual bool DoDeserialize(const class ibDataNode& node) override {
		SetEnumValue(static_cast<valT>(node.GetValue<s32>(kValueFieldData)));
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
	std::map<valT, wxString> m_listEnumData, m_listEnumDesc;
protected:

	template <typename valType>
	class ibValueEnumerationVariant : public ibValueEnumerationVariantBase<valType> {
	public:

		ibValueEnumerationVariant(const valType& v, const ibClassID& clsid) : ibValueEnumerationVariantBase<valType>(), m_clsid(clsid), m_value(v) {}

		void CreateEnumeration(
			const wxString& name, const wxString& descr,
			const valType& v) {
			m_name = name;
			m_description = descr;
			m_value = v;
		}

		virtual valT GetEnumValue() const override { return m_value; }
		virtual void SetEnumValue(const valT& v) override { m_value = v; }

		virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const override {
			ibValuePtr<ibValueEnumeration<valType>> enumOwner(ibValue::CreateAndConvertObjectRef<ibValueEnumeration<valType>>(m_clsid));
			for (auto& e : enumOwner->m_listEnumData) {
				if (e.second.Contains(findData)) {
					ibValueEnumerationVariant<valType>* enumValue = new ibValueEnumerationVariant<valType>(e.first, m_clsid);
					if (enumValue != nullptr) {
						enumValue->CreateEnumeration(
							enumOwner->GetEnumName(e.first),
							enumOwner->GetEnumDescription(e.first),
							e.first
						);
						listValue.push_back(enumValue);
					}
				}
			}
			std::sort(listValue.begin(), listValue.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
			return listValue.size() > 0;
		}

		//operator '=='
		virtual bool CompareValueEQ(const ibValue& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value == compareEnumeration->m_value;
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value == compareEnumerationOwner->GetEnumValue();
			return false;
		}

		//operator '!='
		virtual bool CompareValueNE(const ibValue& cParam) const override {
			ibValueEnumerationVariant<valType>* compareEnumeration = dynamic_cast<ibValueEnumerationVariant<valType> *>(cParam.GetRef());
			if (compareEnumeration) return m_value != compareEnumeration->m_value;
			ibValueEnumeration<valType>* compareEnumerationOwner = dynamic_cast<ibValueEnumeration<valType> *>(cParam.GetRef());
			if (compareEnumerationOwner) return m_value != compareEnumerationOwner->GetEnumValue();
			return true;
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
		virtual wxString GetClassName() const override { return ibValue::GetNameObjectFromID(m_clsid); }

		//type conversion
		// THE DESCRIPTION IS WHAT A PERSON READS — the caption in their own
		// language, translated, not the identifier behind it. The NAME is
		// the identifier a script writes (`ComparisonKind.Equal`) and what a saved
		// setting round-trips; presenting it in a picker or a cell makes the form
		// speak in identifiers. Falls back to the name when a member was declared
		// without a description, so nothing is ever blank.
		virtual wxString GetString() const override {
			return m_description.IsEmpty() ? m_name : m_description;
		}
		// The identifier, for whoever needs it as such.
		const wxString& GetEnumMemberName() const { return m_name; }
		virtual ibNumber GetNumber() const override { return m_value; }

	private:
		wxString m_name;
		wxString m_description;
		// ⭐ THE DEFAULT STATE IS "NO MEMBER", SPELLED — not whatever the memory happened to hold.
		// Every member number is valid, so an uninitialised field is indistinguishable from a
		// choice, and the one thing it can never be is caught. Starting at emptyEnum means the
		// only way to hold a member is to have been given one.
		ibClassID m_clsid = 0;
		valType m_value = static_cast<valType>(emptyEnum);
	};

	virtual wxString GetEnumName(const valT& v) const { return m_listEnumData.at(v); }
	virtual wxString GetEnumDescription(const valT& v) const { return m_listEnumDesc.at(v); }

private:

	inline void CreateEnumeration(const valT& v) {
		wxASSERT(m_listEnumData.find(v) != m_listEnumData.end());
		if (m_value != nullptr) {
			m_value->CreateEnumeration(
				GetEnumName(v),
				GetEnumDescription(v),
				v
			);
		}
	}

public:

	using valEnumType = valT;

	inline void AddEnumeration(const valT& v, const wxString& name, const wxString& descr = wxEmptyString) {
		wxASSERT(m_listEnumData.find(v) == m_listEnumData.end());
		m_listEnumData.insert_or_assign(v, name);
		m_listEnumDesc.insert_or_assign(v, descr.IsEmpty() ? name : descr);

		this->m_listEnumStr.push_back(name);
	}

	ibValueEnumeration() : ibValueEnumerationBase<valT>(true), m_value(nullptr) { InitializeEnumeration(); }
	virtual ~ibValueEnumeration() {}

	virtual bool Init() override { return true; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override {
		if (lSizeArray < 1)
			return false;
		const valT& defValue = static_cast<valT>(paParams[0]->GetInteger());
		ibValueEnumeration::InitializeEnumeration(defValue);
		return true;
	}

	virtual valT GetDefaultEnumValue() const {
		auto itEnums = m_listEnumData.begin();
		std::advance(itEnums, 1);
		if (itEnums != m_listEnumData.end())
			return itEnums->first;
		return valT();
	};

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
	virtual void SetEnumValue(const valT& v) override {
		if (m_value == nullptr) {
			InitializeEnumeration(v);
			return;
		}
		m_value->CreateEnumeration(
			GetEnumName(v),
			GetEnumDescription(v),
			v
		);
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
		node.SetValue(kValueFieldData, m_value != nullptr ? (s32)m_value->GetEnumValue() : (s32)emptyEnum);
		return true;
	}

	virtual bool DoDeserialize(const class ibDataNode& node) override {
		const s32 member = node.GetValue<s32>(kValueFieldData);
		if (member <= emptyEnum) {
			m_value = decltype(m_value)();   // read back as written: an enumeration with no member
			return true;
		}
		InitializeEnumeration(static_cast<valT>(member));
		return true;
	}

	//create enumeration 
	virtual void CreateEnumeration() = 0;

	ibValueEnumerationVariant<valT>* CreateEnumVariantValue(const valT& v) const {

		ibValueEnumerationVariant<valT>* enumValue = new ibValueEnumerationVariant<valT>(v, ibValue::GetClassType());
		if (enumValue != nullptr) {
			enumValue->CreateEnumeration(
				GetEnumName(v),
				GetEnumDescription(v),
				v
			);
			return enumValue;
		}

		return nullptr;
	}

	//initialize enumeration
	void InitializeEnumeration() {
		// Names surface lazily from FillMembers (Build on first GetPMethods);
		// m_listEnumStr is already populated by AddEnumeration above.
		this->m_members.Invalidate();
	}

	void InitializeEnumeration(const valT& v) {
		m_value = CreateEnumVariantValue(v);
		CreateEnumeration(v);
	}

	virtual ibValue* GetEnumVariantValue() const override { return m_value; }

	wxString GetEnumName(unsigned int idx) const {
		if (idx > m_listEnumData.size())
			return wxEmptyString;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->second;
	}

	wxString GetEnumDesc(unsigned int idx) const {
		if (idx > m_listEnumDesc.size())
			return wxEmptyString;
		auto it = m_listEnumDesc.begin();
		std::advance(it, idx);
		return it->second;
	}

	valT GetEnumValue(unsigned int idx) const {
		if (idx > m_listEnumData.size())
			return (valT)0;
		auto it = m_listEnumData.begin();
		std::advance(it, idx);
		return it->first;
	}

	unsigned int GetEnumCount() const { return m_listEnumData.size(); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override { //attribute value
		auto itEnums = m_listEnumData.begin();
		std::advance(itEnums, lPropNum);
		if (itEnums != m_listEnumData.end()) {
			ibValueEnumerationVariant<valT>* enumValue =
				new ibValueEnumerationVariant<valT>(itEnums->first, ibValue::GetClassType());
			if (enumValue != nullptr) {
				enumValue->CreateEnumeration(
					GetEnumName(itEnums->first),
					GetEnumDescription(itEnums->first),
					itEnums->first
				);
				pvarPropVal = enumValue;
				return true;
			}
			return false;
		}
		return false;
	}

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const override {
		for (auto& e : m_listEnumData) {
			if (e.second.Contains(findData)) {
				ibValueEnumerationVariant<valT>* enumValue = new ibValueEnumerationVariant<valT>(e.first, ibValue::GetClassType());
				if (enumValue != nullptr) {
					enumValue->CreateEnumeration(
						GetEnumName(e.first),
						GetEnumDescription(e.first),
						e.first
					);
					listValue.push_back(enumValue);
				}
			}
		}
		std::sort(listValue.begin(), listValue.end(), [](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
		return listValue.size() > 0;
	}

	//operator '=='
	virtual bool CompareValueEQ(const ibValue& cParam) const override {
		if (m_value != nullptr) {
			return m_value->CompareValueEQ(cParam);
		}
		return ibValue::CompareValueEQ(cParam);
	}

	//operator '!='
	virtual bool CompareValueNE(const ibValue& cParam) const override {
		if (m_value != nullptr) {
			return m_value->CompareValueNE(cParam);
		}
		return ibValue::CompareValueNE(cParam);
	}

	// ⭐ NO MEMBER CHOSEN IS EMPTY. This answered NO unconditionally, while the two methods right
	// below already tell the truth about the same state — GetString returns "" and GetNumber returns
	// wxNOT_FOUND when m_value is null. Fill-check asks exactly this question (SaveData), so a
	// required enumeration left unset was saved as if it had been filled in, and the refusal the
	// attribute's flag promised never came. The VARIANT stays non-empty: a chosen member is a value.
	virtual bool IsEmpty() const override { return m_value == nullptr; }

	//type info
	virtual wxString GetClassName() const final {
		return m_value ? m_value->GetClassName() :
			ibValue::GetClassName();
	};

	//type conversion
	virtual wxString GetString() const final {
		return m_value ? m_value->GetString() :
			wxString(wxEmptyString);
	}

	virtual ibNumber GetNumber() const final {
		return m_value ? m_value->GetNumber() :
			emptyEnum;
	}

protected:

	ibValuePtr<ibValueEnumerationVariant<valT>> m_value; //current enum value
};

#endif