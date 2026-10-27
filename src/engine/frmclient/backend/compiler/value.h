#ifndef _FRMCLIENT_BACKEND_VALUE_H__
#define _FRMCLIENT_BACKEND_VALUE_H__

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <wx/icon.h>

#include "frmclient/backend/backend_core.h"   // …as the engine's value.h brings it to those who include it
#include "core/variant.h"                     // ibVariant — the value itself: its word, its count, its lifecycle
#include "core/serialize/dataBuilder.h"       // ibDataNode — what a value of the server's is, as it wrote it
#include "core/core.h"                        // kValueFieldClsid / kValueFieldData, ibCtorObjectType

template <class T> class ibValuePtr;
class ibCtorAbstractType;

// THE VALUE — the engine's ibValue (backend/compiler/value.h) as the client holds one, and lighter: THE VALUE ITSELF IS
// THE VARIANT'S (core/variant.h) — a primitive, or an object held by its count. What this class adds is the client's
// small factory: what it makes of a node the server wrote (a primitive, an object of its own, or the server's value kept
// as it came), the types the server named, and an enumeration's member.
class FRMCLIENT_API ibValue : public ibVariant {
public:

	ibValue() = default;
	ibValue(const ibString& text) : ibVariant(text) {}
	ibValue(const ibChar* text) : ibVariant(text) {}
	// A narrow literal is a text too — without this one `ibValue("x")` is the flag TRUE (pointer-to-bool beats the
	// conversion to wxString), the trap the engine's ctors name.
	ibValue(const char* text) : ibVariant(text) {}
	// …and a wx text, the one a window hands in (a cell of a dialog's grid): without it `ibValue(wxs)` is ambiguous
	// on GCC and Clang — wxString reaches ibString, `const wchar_t*` and bool alike, each one user conversion away.
	ibValue(const wxString& text) : ibVariant(text) {}
	ibValue(bool flag) : ibVariant(flag) {}
	ibValue(int number) : ibVariant(number) {}
	ibValue(long number) : ibVariant(number) {}
	ibValue(unsigned int number) : ibVariant(number) {}
	ibValue(const ibNumber& number) : ibVariant(number) {}
	ibValue(const ibDateTime& date) : ibVariant(date) {}
	// AN OBJECT HELD — the engine's TYPE_REFFER: a value of the client's own kind (a field of a composition), counted
	// while it is held.
	ibValue(ibValue* pParam) : ibVariant(static_cast<ibVariant*>(pParam)) {}
	// …and the base of such an object, of its kind.
	ibValue(ibValueTypes nType, bool readOnly = false) : ibVariant(nType, readOnly) {}
	// A VARIANT AS IT IS — a cell's, a sheet's parameter read back.
	explicit ibValue(const ibVariant& variant) : ibVariant(variant) {}

	// THE CLIENT'S SMALL FACTORY.

	// INTO A NODE AND BACK — the engine's form ({t, v}): FromNode makes a primitive of a primitive's node, the client's
	// own object of its own (a field of a composition, a colour, a font), and keeps any other as the server wrote it.
	static ibValue FromNode(const ibDataNode& node);

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

	// The types the server named — their NAMES and PICTURES as it wrote them with them (a type property's offer), what a
	// type is offered by (the type picker). The client has no registry of types.
	static ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid);
	static void RegisterCtor(const ibClassID& clsid, const ibString& className, const wxIcon& classIcon = wxNullIcon);

	// The registered types of a kind — the engine's registry; the client's are the ones the server's question named (a
	// form editor's Classes: the controls' names and pictures). Held until the next ask.
	static std::vector<ibCtorAbstractType*> GetListCtorsByType(ibCtorObjectType objectType = ibCtorObjectType::ibCtorObjectType_object_value);

	// The engine's own picture of a value (value_res.cpp) — what a field of a settings window wears.
	static wxIcon GetIconGroup();

	// THE VALUES THIS ONE IS PICKED AMONG — a quick choice: a flag's two, a text read as this kind, an enumeration's members.
	virtual bool FindValue(const ibString& findData, std::vector<ibValue>& listValue) const;

	// Opened — a cell's details drilled into: the server's to do (the grid box sends the cell it was asked on).
	void ShowValue() const {}
};

// A VALUE OF A TYPE OF THE SERVER'S — a reference, a value of a type the client does not know: held as the server wrote
// it, its node given back untouched, read as the server presents it (Presentation: the server's GetString — a
// reference's name is the configuration's).
class ibValueSerializable : public ibValue {
public:

	ibValueSerializable(const ibClassID& clsid, const ibDataNode& node, const ibString& presentation)
		: ibValue(ibValueTypes::TYPE_VALUE), m_classType(clsid), m_node(node), m_presentation(presentation) {}

	virtual ibClassID GetClassType() const override { return m_classType; }
	virtual ibString GetString() const override { return m_presentation; }

	// Two are the same when the server wrote them the same: their class, and their payloads compared as text.
	virtual bool CompareValueEQ(const ibVariant& value) const override;

protected:

	// The server's node, given back as it came.
	virtual bool DoSerialize(ibDataNode& node) const override { node = m_node; return true; }

private:

	ibClassID  m_classType;
	ibDataNode m_node;
	ibString   m_presentation;
};

// A REGISTERED TYPE — the engine's ibCtorAbstractType (backend/compiler/typeCtor.h) as far as a window lists one: its
// name and its picture.
class ibCtorAbstractType {
public:
	ibCtorAbstractType(const ibString& className, const wxIcon& classIcon) : m_className(className), m_classIcon(classIcon) {}

	const ibString& GetClassName() const { return m_className; }
	wxIcon GetClassIcon() const { return m_classIcon; }

private:
	ibString m_className;
	wxIcon   m_classIcon;
};

// A HELD OBJECT — the engine's ibValuePtr (backend/value_ptr.h) as it is: a value that holds a T by its count and gives
// typed access to it.
template <class T>
class ibValuePtr : public ibValue {

	inline void OnSetValueT(ibVariant* ptr) {
		// Self-assignment guard: skip when already bound to the same referent.
		if (m_pRef != ptr && !m_bReadOnly) {
			Reset();
			if (ptr != nullptr) {
				m_typeClass = ibValueTypes::TYPE_REFFER;
				m_pRef = ptr;
				m_pRef->IncrRef();
			}
		}
	}

public:

	ibValuePtr() = default;
	ibValuePtr(std::nullptr_t) {}
	explicit ibValuePtr(T* ptr) { OnSetValueT(ptr); }
	ibValuePtr(const ibValuePtr& to_copy) : ibValue() { OnSetValueT(to_copy.Ptr()); }

	inline T& operator*() const { return *Ptr(); }
	inline T* operator->() const { return Ptr(); }

	inline operator T* () const { return Ptr(); }
	inline explicit operator bool() const noexcept { return m_pRef != nullptr; }

	void operator = (T* ptr) { OnSetValueT(ptr); }
	void operator = (const ibValuePtr& other) { OnSetValueT(other.Ptr()); }

private:

	inline T* Ptr() const { return static_cast<T*>(m_pRef); }
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
