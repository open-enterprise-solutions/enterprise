#ifndef __PROPERTY_VALUE_H__
#define __PROPERTY_VALUE_H__

////////////////////////////////////////////////////////////////////////////
// ibPropertyValue — A PROPERTY'S VALUE, made the way the runtime's ibValue is: one word that is a flag, a number, a
// date, a text — or a REFERENCE, counted, to a live value: a type description, a source, a picture, which keeps itself
// current (a type refreshes against the configuration it belongs to).
////////////////////////////////////////////////////////////////////////////
//
// A live value is a class derived from this one, made with TYPE_VALUE, as a runtime object is derived from ibValue; a
// property holds it by reference, and a copy of the property's value is one more holder of the same live value, which
// sees it change. It is no runtime value — no class of the factory, nothing a script is handed (that is GetDataValue's) —
// so it answers what ibAnyValue asks, and whether two are equal.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "core/anyValue.h"   // ibAnyValue — what every value answers, on either side

#include <atomic>
#include <type_traits>
#include <utility>

class BACKEND_API ibPropertyValue : public ibAnyValue {
public:

	ibPropertyValue() : m_dData() {}
	ibPropertyValue(const ibPropertyValue& value) : m_dData() { Copy(value); }
	ibPropertyValue(ibPropertyValue&& value) noexcept : m_dData() { Move(std::move(value)); }

	// A live value, held by its count.
	ibPropertyValue(ibPropertyValue* value) : m_dData() {
		if (value != nullptr) {
			m_typeClass = ibValueTypes::TYPE_REFFER;
			m_pRef = value;
			m_pRef->IncrRef();
		}
	}

	ibPropertyValue(bool value) : m_typeClass(ibValueTypes::TYPE_BOOLEAN), m_dData() { m_bData = value; }
	ibPropertyValue(signed int value) : ibPropertyValue(ibNumber(value)) {}
	ibPropertyValue(unsigned int value) : ibPropertyValue(ibNumber(value)) {}
	ibPropertyValue(long value) : ibPropertyValue(ibNumber(value)) {}
	ibPropertyValue(const ibNumber& value) : m_typeClass(ibValueTypes::TYPE_NUMBER), m_dData() { m_fData = value; }
	ibPropertyValue(const ibDateTime& value) : m_typeClass(ibValueTypes::TYPE_DATE), m_dData() { m_dData = value; }
	ibPropertyValue(const wchar_t* value) : ibPropertyValue(ibString(value)) {}
	ibPropertyValue(const ibString& value) : m_typeClass(ibValueTypes::TYPE_STRING), m_dData() { m_sData = value; }
	ibPropertyValue(ibString&& value) : m_typeClass(ibValueTypes::TYPE_STRING), m_dData() { m_sData = std::move(value); }

	// ANY OTHER POINTER IS A MISTAKE, as it is for ibValue: pointer-to-bool is a standard conversion, and an unrelated
	// pointer would arrive as TRUE. Only a live value (derived from this class) and a text are taken.
	template <class T, class = typename std::enable_if<
		!std::is_same<typename std::remove_cv<T>::type, wchar_t>::value &&
		!std::is_base_of<ibPropertyValue, T>::value>::type>
	ibPropertyValue(T*) = delete;

	virtual ~ibPropertyValue() { Release(); }

	ibPropertyValue& operator=(const ibPropertyValue& value) { if (this != &value) Copy(value); return *this; }
	ibPropertyValue& operator=(ibPropertyValue&& value) noexcept { if (this != &value) Move(std::move(value)); return *this; }

	bool operator==(const ibPropertyValue& value) const { return CompareValueEQ(value); }
	bool operator!=(const ibPropertyValue& value) const { return !CompareValueEQ(value); }

	// The same kind and the same content — a live value answers for its own (and a reference passes the question on).
	virtual bool CompareValueEQ(const ibPropertyValue& value) const {
		if (m_typeClass == ibValueTypes::TYPE_REFFER)
			return m_pRef->CompareValueEQ(value);
		if (value.m_typeClass == ibValueTypes::TYPE_REFFER)
			return value.m_pRef->CompareValueEQ(*this);
		if (m_typeClass != value.m_typeClass)
			return false;
		switch (m_typeClass) {
		case ibValueTypes::TYPE_BOOLEAN: return m_bData == value.m_bData;
		case ibValueTypes::TYPE_NUMBER: return m_fData == value.m_fData;
		case ibValueTypes::TYPE_DATE: return m_dData == value.m_dData;
		case ibValueTypes::TYPE_STRING: return m_sData == value.m_sData;
		case ibValueTypes::TYPE_EMPTY: return true;
		default: return this == &value;   // a live value that does not answer: only itself
		}
	}

	virtual ibValueTypes GetType() const override {
		return m_typeClass == ibValueTypes::TYPE_REFFER ? m_pRef->GetType() : m_typeClass;
	}

	virtual bool IsEmpty() const override {
		switch (m_typeClass) {
		case ibValueTypes::TYPE_BOOLEAN: return !m_bData;
		case ibValueTypes::TYPE_NUMBER: return m_fData.IsZero();
		case ibValueTypes::TYPE_DATE: return m_dData.IsEmpty();
		case ibValueTypes::TYPE_STRING: return m_sData.IsEmpty();
		case ibValueTypes::TYPE_REFFER: return m_pRef->IsEmpty();
		case ibValueTypes::TYPE_VALUE: return false;
		default: return true;
		}
	}

	virtual bool GetBoolean() const override {
		switch (m_typeClass) {
		case ibValueTypes::TYPE_BOOLEAN: return m_bData;
		case ibValueTypes::TYPE_NUMBER: return !m_fData.IsZero();
		case ibValueTypes::TYPE_REFFER: return m_pRef->GetBoolean();
		default: return false;
		}
	}

	virtual ibNumber GetNumber() const override {
		switch (m_typeClass) {
		case ibValueTypes::TYPE_NUMBER: return m_fData;
		case ibValueTypes::TYPE_BOOLEAN: return m_bData ? 1 : 0;
		case ibValueTypes::TYPE_REFFER: return m_pRef->GetNumber();
		default: return ibNumber();
		}
	}

	int GetInteger() const { return GetNumber().ToInt(); }

	virtual ibDateTime GetDate() const override {
		switch (m_typeClass) {
		case ibValueTypes::TYPE_DATE: return m_dData;
		case ibValueTypes::TYPE_REFFER: return m_pRef->GetDate();
		default: return ibDateTime();
		}
	}

	virtual ibString GetString() const override {
		switch (m_typeClass) {
		case ibValueTypes::TYPE_STRING: return m_sData;   // the text shared — one more owner, no copy
		case ibValueTypes::TYPE_BOOLEAN: return m_bData.ToString();
		case ibValueTypes::TYPE_NUMBER: return m_fData.ToString();
		case ibValueTypes::TYPE_DATE: return m_dData.ToString();
		case ibValueTypes::TYPE_REFFER: return m_pRef->GetString();
		default: return ibString();
		}
	}

	// The live value held — or this one, holding none (as ibValue::GetRef).
	ibPropertyValue* GetRef() const {
		return m_typeClass == ibValueTypes::TYPE_REFFER ? m_pRef : const_cast<ibPropertyValue*>(this);
	}

	template <typename valueType> inline valueType* ConvertToType() const {
		return dynamic_cast<valueType*>(GetRef());
	}

	virtual void IncrRef() override { m_refCount.fetch_add(1, std::memory_order_relaxed); }
	virtual void DecrRef() override {
		wxASSERT_MSG(m_refCount.load(std::memory_order_relaxed) > 0, "invalid ref data count");
		if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
	}

	void Reset() {
		Release();
		m_typeClass = ibValueTypes::TYPE_EMPTY;
		m_dData = ibDateTime();   // the whole word: the empty state of every member (below)
	}

protected:

	// A live value is made with TYPE_VALUE, as a runtime object is.
	explicit ibPropertyValue(ibValueTypes type) : m_typeClass(type), m_dData() {}

private:

	void Release() {
		if (m_typeClass == ibValueTypes::TYPE_REFFER)
			m_pRef->DecrRef();
		else if (m_typeClass == ibValueTypes::TYPE_STRING)
			m_sData.~ibString();
		else if (m_typeClass == ibValueTypes::TYPE_NUMBER)
			m_fData.~ibNumber();
	}

	// A live value is not copied by its holder: the holder takes one more reference to it (a class derived from this
	// one copies itself — its Clone — and the copy starts uncounted, with no holders).
	void Copy(const ibPropertyValue& value) {
		Reset();
		switch (value.m_typeClass) {
		case ibValueTypes::TYPE_BOOLEAN: m_bData = value.m_bData; break;
		case ibValueTypes::TYPE_NUMBER: m_fData = value.m_fData; break;
		case ibValueTypes::TYPE_DATE: m_dData = value.m_dData; break;
		case ibValueTypes::TYPE_STRING: m_sData = value.m_sData; break;
		case ibValueTypes::TYPE_REFFER: m_pRef = value.m_pRef; m_pRef->IncrRef(); break;
		default: break;
		}
		m_typeClass = value.m_typeClass;
	}

	void Move(ibPropertyValue&& value) {
		if (value.m_typeClass != ibValueTypes::TYPE_REFFER && value.m_typeClass != ibValueTypes::TYPE_STRING
			&& value.m_typeClass != ibValueTypes::TYPE_NUMBER) {
			Copy(value);
			return;
		}
		Reset();
		switch (value.m_typeClass) {
		case ibValueTypes::TYPE_STRING: m_sData = std::move(value.m_sData); value.m_sData.~ibString(); break;
		case ibValueTypes::TYPE_NUMBER: m_fData = std::move(value.m_fData); value.m_fData.~ibNumber(); break;
		default: m_pRef = value.m_pRef; break;   // the reference handed over, its count as it was
		}
		m_typeClass = value.m_typeClass;
		value.m_typeClass = ibValueTypes::TYPE_EMPTY;
		value.m_dData = ibDateTime();
	}

	ibValueTypes m_typeClass = ibValueTypes::TYPE_EMPTY;
	std::atomic<unsigned int> m_refCount { 0 };

	// ONE WORD FOR EVERY KIND, as ibValue's (backend/compiler/value.h, the union): its empty state is all-zero bits, a
	// valid value of every member; Reset ends the life of a text or a number and zeroes it again.
	union {
		ibBoolean        m_bData;   // TYPE_BOOLEAN
		ibDateTime       m_dData;   // TYPE_DATE — one word; zeroing it zeroes the union
		ibPropertyValue* m_pRef;    // TYPE_REFFER — a live value, counted
		ibString         m_sData;   // TYPE_STRING
		ibNumber         m_fData;   // TYPE_NUMBER
	};
};

#endif // !__PROPERTY_VALUE_H__
