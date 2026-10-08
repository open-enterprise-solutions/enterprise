#ifndef _FRMCLIENT_BACKEND_VALUE_H__
#define _FRMCLIENT_BACKEND_VALUE_H__

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <wx/icon.h>

#include "frmclient/backend/backend_core.h"   // …as the engine's value.h brings it to those who include it
#include "core/fileSystem/types.h"
#include "core/types.h"   // kValueFieldClsid / kValueFieldData — the fields a value is written into a node by

template <class T> class ibValuePtr;
class ibDataNode;
class ibCtorAbstractType;

constexpr ibClassID g_valueBooleanCLSID = primitive_to_clsid("VL_BOOL");
constexpr ibClassID g_valueNumberCLSID = primitive_to_clsid("VL_NUMB");
constexpr ibClassID g_valueDateCLSID = primitive_to_clsid("VL_DATE");
constexpr ibClassID g_valueStringCLSID = primitive_to_clsid("VL_STRI");

constexpr ibClassID g_valueNullCLSID = primitive_to_clsid("VL_NULL");

// THE VALUE — the engine's ibValue (backend/compiler/value.h) as the client holds one. There is no runtime here and no
// base: a PRIMITIVE — a text, a number, a date, a flag — is data a person types, and is held as it is; a value of a type
// the SERVER has (a reference, an enum member) is held as the server wrote it — its node, carried and given back
// untouched — and read as the server presents it. An object of the client's own a property reads its choices from (an
// enumeration) is one too.
class ibValue {
public:

	ibValue() = default;
	ibValue(const wxString& text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	ibValue(const wxChar* text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	ibValue(bool flag) : m_typeClass(ibValueTypes::TYPE_BOOLEAN), m_text(flag ? wxT("True") : wxT("False")), m_number(flag ? 1 : 0) {}
	ibValue(int number) : m_typeClass(ibValueTypes::TYPE_NUMBER), m_text(wxString::Format(wxT("%d"), number)), m_number(number), m_numeric(number) {}
	ibValue(long number) : m_typeClass(ibValueTypes::TYPE_NUMBER), m_text(wxString::Format(wxT("%ld"), number)), m_number(number), m_numeric(number) {}
	ibValue(unsigned int number) : m_typeClass(ibValueTypes::TYPE_NUMBER), m_text(wxString::Format(wxT("%u"), number)), m_number(static_cast<long>(number)), m_numeric(number) {}
	ibValue(const ibNumber& number);
	ibValue(const ibDateTime& date);
	// AN OBJECT HELD — the engine's TYPE_REFFER: a value of the client's own kind (a field of a composition) carried in a
	// value, counted while it is.
	ibValue(ibValue* pParam);
	// …and the base of such an object, of its kind.
	ibValue(ibValueTypes nType, bool WXUNUSED(readOnly) = false) : m_typeClass(nType) {}
	ibValue(const ibValue& rhs) { Assign(rhs); }
	ibValue& operator=(const ibValue& rhs) { Release(); Assign(rhs); return *this; }
	virtual ~ibValue() { Release(); }

	// What it is — a primitive, Undefined, or a type of the server's (GetClassType says which).
	ibValueTypes GetType() const { return m_typeClass; }
	virtual ibClassID GetClassType() const;
	virtual bool IsEmpty() const;

	virtual bool CompareValueEQ(const ibValue& cParam) const;
	virtual bool CompareValueNE(const ibValue& cParam) const { return !CompareValueEQ(cParam); }
	bool operator==(const ibValue& rhs) const { return CompareValueEQ(rhs); }
	bool operator!=(const ibValue& rhs) const { return CompareValueNE(rhs); }

	// The object it holds — the engine's: itself when it holds none.
	inline bool IsReference() const {
		return m_typeClass == ibValueTypes::TYPE_REFFER || m_typeClass == ibValueTypes::TYPE_CONST_REFFER;
	}
	virtual ibValue* GetRef() const;

	//convert to value
	template <typename T> inline bool ConvertToValue(T*& ptr) const {
		if (IsReference()) {
			ptr = dynamic_cast<T*>(GetRef());
			return ptr != nullptr;
		}
		else if (m_typeClass != ibValueTypes::TYPE_EMPTY) {
			ptr = dynamic_cast<T*>(const_cast<ibValue*>(this));
			return ptr != nullptr;
		}
		return false;
	}
	template <class T>
	T* ConvertToType() const { T* ptr = nullptr; return ConvertToValue(ptr) ? ptr : nullptr; }

	virtual ibString GetString() const;
	// …and AS another primitive — the engine's conversions: a text typed into a cell read as the number, the date or the
	// flag it spells.
	bool GetBoolean() const;
	long GetInteger() const { return m_number; }
	unsigned int GetUInteger() const { return static_cast<unsigned int>(m_number); }
	ibNumber GetNumber() const;
	ibDateTime GetDate() const;

	// INTO A NODE AND BACK — the engine's form ({t, v}): a primitive writes its payload, a value of the server's writes
	// back the node it came in; FromNode makes a primitive of a primitive's node and keeps any other as it was written.
	bool Serialize(ibDataNode& node) const;
	static ibValue FromNode(const ibDataNode& node);

	// The primitives' types by their class ids, both ways — the engine's; a type of the server's is none of them.
	static ibClassID GetIDByVT(const ibValueTypes& valueType);
	static ibValueTypes GetVTByID(const ibClassID& clsid);
	// The client has no registry of types: a primitive's is known, the rest are the server's.
	static bool IsRegisterCtor(const ibClassID& clsid) { return GetVTByID(clsid) != ibValueTypes::TYPE_VALUE; }
	// …and their NAMES are the ones the server wrote with them (a type description's TypeName) — kept as they come, what
	// a type is offered by (the type picker).
	static ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid);
	static void RegisterCtor(const ibClassID& clsid, const wxString& className);
	template <typename valT>
	valT ConvertToEnumValue() const {
		if (IsReference() && m_pRef != nullptr)
			return m_pRef->ConvertToEnumValue<valT>();
		return static_cast<valT>(m_number);
	}

	// THE VALUES THIS ONE IS PICKED AMONG — the engine's FindValue with nothing typed (a quick choice): a flag's two, an
	// enumeration's members. A text typed is looked for among them; what a base holds is the server's to find.
	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& foundedObjects) const;

	// A MEMBER OF AN ENUMERATION as a value — the engine's: it reads as its word, is its number, and is written as the
	// enumeration's class and that number (enumUnit.h, where T is complete).
	template <class T, typename valT = typename T::valEnumType>
	static ibValue CreateEnumObject(const valT& v);

	// An object of the client's own, made ready for reading (Init).
	template <class T>
	static ibValuePtr<T> CreateObject() {
		T* const made = new T();
		made->Init();
		return ibValuePtr<T>(made);
	}
	// …and one made of what it holds (a font, a colour).
	template <class T, class... Args>
	static ibValuePtr<T> CreateObjectValue(Args&&... args) {
		return ibValuePtr<T>(new T(std::forward<Args>(args)...));
	}
	virtual bool Init() { return true; }

	// Opened — a cell's details drilled into: the server's to do (the grid box sends the cell it was asked on).
	void ShowValue() const {}

	// Counted, as the engine's: whoever holds it (ibValuePtr) keeps it.
	void IncrRef() { m_refCount++; }
	void DecrRef() { if (--m_refCount == 0) delete this; }

	// The engine's own picture of a value (value_res.cpp) — what a field of a settings window wears.
	static wxIcon GetIconGroup();

	// The registered types of a kind — the engine's registry; the client's are the ones the server's question named (a
	// form editor's Classes: the controls' names and pictures). Held until the next ask.
	static std::vector<class ibCtorAbstractType*> GetListCtorsByType(ibCtorObjectType objectType = ibCtorObjectType::ibCtorObjectType_object_value);

protected:

	// How an object of the client's own writes itself into a node and reads itself back — the engine's.
	virtual bool DoSerialize(class ibDataNode& node) const;
	virtual bool DoDeserialize(const class ibDataNode& WXUNUSED(node)) { return false; }

	// A member of an enumeration — the base of one (ibValueEnumerationVariant, enumUnit.h).
	ibValue(const ibClassID& enumClass, long member, const wxString& word)
		: m_typeClass(ibValueTypes::TYPE_ENUM), m_classType(enumClass), m_text(word), m_number(member) {}

private:

	// Everything but the count — a copy is not held by whoever held the original; the object it holds is, once more.
	void Assign(const ibValue& rhs);
	void Release();

	ibValueTypes m_typeClass = ibValueTypes::TYPE_EMPTY;
	ibClassID    m_classType = 0;   // a type of the server's — its node is kept in m_stored
	wxString     m_text;            // the text, or how the value reads
	long         m_number = 0;
	ibNumber     m_numeric;
	ibDateTime   m_date;
	std::shared_ptr<const ibDataNode> m_stored;   // a value of the server's type, as it wrote it
	ibValue*     m_pRef = nullptr;  // TYPE_REFFER — the object held
	unsigned     m_refCount = 0;
};

// A REGISTERED TYPE — the engine's ibCtorAbstractType (backend/compiler/typeCtor.h) as far as a window lists one: its
// name and its picture.
class ibCtorAbstractType {
public:
	ibCtorAbstractType(const wxString& className, const wxIcon& classIcon) : m_className(className), m_classIcon(classIcon) {}

	wxString GetClassName() const { return m_className; }
	wxIcon GetClassIcon() const { return m_classIcon; }

private:
	wxString m_className;
	wxIcon   m_classIcon;
};

// A HELD OBJECT — the engine's ibValuePtr (backend/value_ptr.h): the value counted while it is held.
template <class T>
class ibValuePtr : public ibValue {
public:

	ibValuePtr() = default;
	ibValuePtr(T* ref) : m_pRef(ref) { if (m_pRef != nullptr) m_pRef->IncrRef(); }
	ibValuePtr(const ibValuePtr& rhs) : ibValue(rhs), m_pRef(rhs.m_pRef) { if (m_pRef != nullptr) m_pRef->IncrRef(); }
	ibValuePtr& operator=(const ibValuePtr& rhs) {
		if (rhs.m_pRef != nullptr) rhs.m_pRef->IncrRef();
		if (m_pRef != nullptr) m_pRef->DecrRef();
		m_pRef = rhs.m_pRef;
		return *this;
	}
	virtual ~ibValuePtr() { if (m_pRef != nullptr) m_pRef->DecrRef(); }

	T* operator->() const { return m_pRef; }
	operator T* () const { return m_pRef; }

private:

	T* m_pRef = nullptr;
};

// HELD OBJECTS COMPARED AS POINTERS — the engine's (backend/value_ptr.h): a held object is a value too, and without these
// a comparison would reach for the value's own operator== and ask whether the two VALUES are equal.
template <class _Ty1, class _Ty2>
inline bool operator==(const ibValuePtr<_Ty1>& _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return static_cast<_Ty1*>(_Left) == static_cast<_Ty2*>(_Right);
}

template <class _Ty1, class _Ty2>
inline bool operator!=(const ibValuePtr<_Ty1>& _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return static_cast<_Ty1*>(_Left) != static_cast<_Ty2*>(_Right);
}

template <class _Ty>
inline bool operator==(std::nullptr_t, const ibValuePtr<_Ty>& _Right) noexcept {
	return static_cast<_Ty*>(_Right) == nullptr;
}

template <class _Ty>
inline bool operator!=(std::nullptr_t, const ibValuePtr<_Ty>& _Right) noexcept {
	return static_cast<_Ty*>(_Right) != nullptr;
}

template <class _Ty>
inline bool operator==(const ibValuePtr<_Ty>& _Left, std::nullptr_t) noexcept {
	return static_cast<_Ty*>(_Left) == nullptr;
}

template <class _Ty>
inline bool operator!=(const ibValuePtr<_Ty>& _Left, std::nullptr_t) noexcept {
	return static_cast<_Ty*>(_Left) != nullptr;
}

template <class _Ty1, class _Ty2>
inline bool operator==(const ibValuePtr<_Ty1>& _Left, _Ty2* _Right) noexcept {
	return static_cast<_Ty1*>(_Left) == _Right;
}

template <class _Ty1, class _Ty2>
inline bool operator==(_Ty1* _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return _Left == static_cast<_Ty2*>(_Right);
}

template <class _Ty1, class _Ty2>
inline bool operator!=(_Ty1* _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return _Left != static_cast<_Ty2*>(_Right);
}

template <class _Ty1, class _Ty2>
inline bool operator!=(const ibValuePtr<_Ty1>& _Left, _Ty2* _Right) noexcept {
	return static_cast<_Ty1*>(_Left) != _Right;
}

#endif
