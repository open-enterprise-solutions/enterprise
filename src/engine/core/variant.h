#ifndef _CORE_VARIANT_H__
#define _CORE_VARIANT_H__

////////////////////////////////////////////////////////////////////////////
// ibVariant — THE VALUE: one word that is a flag, a number, a date, a text — or a REFERENCE, counted, to an object.
// A literal (the translator's constant), a property's value and the thin client's value are each one; the engine's
// runtime ibValue (backend/compiler/value.h) derives from it and adds the runtime: the factory, the members, the calls.
////////////////////////////////////////////////////////////////////////////
//
// An OBJECT is a class derived from this one, made with its kind (TYPE_VALUE, TYPE_ENUM), and a variant holds it by
// reference: a copy of the variant is one more holder of the same object, which sees it change. What an object is
// (its class, how it reads, what it equals) it answers itself; what a side makes of a node it alone knows is that
// side's (the client's ibCtorAbstractType, the server's registry) — the core has no factory.
//
// Counted by its own count: no smart pointer holds one, a variant (or ibVariantPtr, core/variant_ptr.h) does.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fdatetime.h"
#include "core/fnumber.h"
#include "core/fstring.h"
#include "core/fboolean.h"   // ibBoolean — the flag in the word

#include <atomic>
#include <string>
#include <type_traits>   // the pointer catch-all below (enable_if / is_base_of / is_same)

class ibDataNode;

class CORE_API ibVariant {
public:
	//ATTRIBUTES:
	ibValueTypes m_typeClass;  // 1 byte (enum : unsigned char)
	bool m_bReadOnly;          // 1 byte
private:
	// ⭐ THE COUNT FILLS THE HOLE BEFORE THE UNION. The two bytes above leave the rest of their word
	// empty (the union is 8-aligned); declared at the end of the class the count took a word of its
	// own there, so x64 paid 8 bytes of padding on every value: vptr 8 + 1 + 1 + 6 empty + union 8 +
	// count 4 + 4 empty = 32. Here it is 8 + 1 + 1 + 2 + 4 + 8 = 24 (x86 stays 24 either way).
	// std::atomic (not wxAtomicInt) — same 4 bytes, but a defined memory model. Never copied / moved:
	// the copy/move ctors value-init it to 0, Copy/Move only touch the payload.
	std::atomic<unsigned int> m_refCount;
public:
	// ⭐ ONE WORD FOR EVERY KIND. A string and a number live IN the union beside the pointers:
	// each is one handle whose heap part (the text, a heap-tier BigImpl) counts its owners, so a
	// copy of a value is a count and not a copy — and the number no longer takes 8 bytes of its own.
	//
	// 🛑 THE UNION'S EMPTY STATE IS ALL-ZERO BITS, and it is a valid value of EVERY member: a null
	// pointer, a false, the empty date (fdatetime.h: count 0), the empty string (no text) and the number
	// 0 (fnumber.h: tag 0 = immediate). The constructors zero the whole word (m_dData() — 8 bytes on x86
	// as well, where a pointer is 4); Reset() ends the life of a string or a number — letting go of its text — and
	// zeroes it again. So whatever member is written next writes over a valid empty one. Never write
	// a string or a number member over a non-zero word of another kind: its old text would be kept.
	union {
		ibBoolean        m_bData;  //TYPE_BOOLEAN — one byte
		ibDateTime       m_dData;  //TYPE_DATE — one word, the count of a wall-clock reading
		ibVariant*       m_pRef;   //TYPE_REFFER (+ VALUE/ENUM/OLE/... aliased)
		// TYPE_CONST_REFFER — read-only view of a NON-owned object (const
		// ibValueMetaObject* from the metadata tree). Aliases m_pRef in storage
		// (same 8 bytes), but the const type documents intent and Reset()/dtor
		// must NEVER ref-count or delete through it — the tree owns the object.
		const ibVariant* m_pConstRef;
		ibString         m_sData;  //TYPE_STRING — a handle on a shared text; empty = no text
		ibNumber         m_fData;  //TYPE_NUMBER — one tagged word; a heap tier is shared
	};
public:

	ibVariant();
	ibVariant(const ibVariant& cParam);
	ibVariant(ibVariant&& cParam);
	ibVariant(ibVariant* pParam);
	ibVariant(ibValueTypes nType, bool readOnly = false);
	ibVariant(bool cParam); //boolean
	ibVariant(signed int cParam); //number
	ibVariant(unsigned int cParam); //number
	ibVariant(long cParam); //number
	ibVariant(double cParam); //number
	ibVariant(const ibNumber& cParam); //number
	ibVariant(const ibDateTime& cParam); //date
	ibVariant(int nYear, int nMonth, int nDay, unsigned short nHour = 0, unsigned short nMinute = 0, unsigned short nSecond = 0); //date
	ibVariant(const char* sParam); //string
	ibVariant(const ibChar* sParam); //string
	ibVariant(const std::wstring& sParam); //string
	ibVariant(const ibString& sParam); //string
	ibVariant(ibString&& sParam); //string — native move
	// …and a wx text, the one a window hands in: without it `ibVariant(wxs)` is ambiguous on GCC and Clang — wxString
	// reaches ibString, `const wchar_t*` and bool alike, each one user conversion away.
	ibVariant(const wxString& sParam); //string

	// ANY OTHER POINTER IS A MISTAKE, the runtime's rule: pointer-to-bool is a standard conversion, and an unrelated
	// pointer would arrive as TRUE. Only an object (derived from this class) and a text are taken.
	template <class T, class = typename std::enable_if<
		!std::is_same<typename std::remove_cv<T>::type, char>::value &&
		!std::is_same<typename std::remove_cv<T>::type, ibChar>::value &&
		!std::is_base_of<ibVariant, T>::value>::type>
	ibVariant(T*) = delete;

	virtual ~ibVariant();

	void Reset();

	// Counted, as the runtime counts its own.
	virtual void IncrRef() { m_refCount.fetch_add(1, std::memory_order_relaxed); }
	virtual void DecrRef() {
		if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
	}

	void operator = (const ibVariant& cParam);
	void operator = (ibVariant&& cParam);
	void operator = (bool cParam);
	void operator = (short cParam);
	void operator = (unsigned short cParam);
	void operator = (int cParam);
	void operator = (unsigned int cParam);
	void operator = (long cParam);
	void operator = (float cParam);
	void operator = (double cParam);
	void operator = (const ibNumber& cParam);
	void operator = (const ibDateTime& cParam);
	void operator = (const ibString& cParam);
	void operator = (const wxString& cParam);
	void operator = (const char* cParam);
	void operator = (const ibChar* cParam);
	void operator = (ibString&& cParam);
	void operator = (ibValueTypes cParam);
	void operator = (ibVariant* pParam);
	void operator = (const ibVariant* pParam);

	bool operator > (const ibVariant& cParam) const { return CompareValueGT(cParam) > 0; }
	bool operator >= (const ibVariant& cParam) const { return CompareValueGE(cParam); }
	bool operator < (const ibVariant& cParam) const { return CompareValueLS(cParam) < 0; }
	bool operator <= (const ibVariant& cParam) const { return CompareValueLE(cParam); }
	bool operator == (const ibVariant& cParam) const { return CompareValueEQ(cParam); }
	bool operator != (const ibVariant& cParam) const { return CompareValueNE(cParam); }

	// The runtime's compare family: '<' is the three-way primitive, '==' type-strict, '!=' its negation.
	virtual int  CompareValueLS(const ibVariant& cParam) const;
	virtual int  CompareValueGT(const ibVariant& cParam) const;
	virtual bool CompareValueGE(const ibVariant& cParam) const;
	virtual bool CompareValueLE(const ibVariant& cParam) const;
	virtual bool CompareValueEQ(const ibVariant& cParam) const;
	virtual bool CompareValueNE(const ibVariant& cParam) const;

	// The object held — or this one, holding none.
	virtual ibVariant* GetRef() const;

	template <typename T> inline bool ConvertToValue(T*& ptr) const {
		if (IsReference()) {
			ptr = dynamic_cast<T*>(GetRef());
			return ptr != nullptr;
		}
		else if (m_typeClass != ibValueTypes::TYPE_EMPTY) {
			ptr = dynamic_cast<T*>(const_cast<ibVariant*>(this));
			return ptr != nullptr;
		}
		return false;
	}
	template <typename T> inline T* ConvertToType() const { T* ptr = nullptr; return ConvertToValue(ptr) ? ptr : nullptr; }

	// A member of an enumeration as its number (an enumeration's member answers GetNumber with it).
	template <typename valT> inline valT ConvertToEnumValue() const { return static_cast<valT>(GetInteger()); }

	inline bool IsReference() const {
		return m_typeClass == ibValueTypes::TYPE_REFFER
			|| m_typeClass == ibValueTypes::TYPE_CONST_REFFER;
	}
	inline bool IsConstReference() const {
		return m_typeClass == ibValueTypes::TYPE_CONST_REFFER;
	}

	//special copy & move function
	void Copy(const ibVariant& cOld);
	void Move(ibVariant&& cOld);

	// INTO A NODE AND BACK — the runtime's form ({clsid, data}): the header is the base's, the contents the value's own
	// (DoSerialize). FromNode makes a primitive of a primitive's node; an object is made by the side that knows its
	// class (the core has no factory), and here reads as empty.
	bool Serialize(ibDataNode& node) const;
	bool Deserialize(const ibDataNode& node);
	static ibVariant FromNode(const ibDataNode& node);

	// May this value cross into ANOTHER session?
	//
	// Everything the interpreter passes around within one session is fine; the
	// question only arises at a session boundary — handing arguments to a
	// background job, which runs on its own session and its own thread. Nothing
	// is serialised on the way (one process, one address space), so what is being
	// asked is about OWNERSHIP, not transport: is this value safe for a second
	// session to hold and read?
	//
	// Default YES, because the overwhelming majority of values are — numbers,
	// strings, dates, references, enumeration values: immutable, owned by nobody.
	// A type says NO when it is either
	//   - MUTABLE and owned by its session (a form, an open recordset, a live
	//     object): two sessions mutating one object coordinate through nothing; or
	//   - BOUND to its session's runtime (a lambda holds m_parentBc, a pointer
	//     into the compiling session's bytecode — which is per-session, built by
	//     CompileRoot; an iterator is a cursor over somebody else's collection; an
	//     OLE handle belongs to the thread that created it).
	//
	// Answering here rather than switching on the tag at the boundary is what
	// keeps the rule with the type: a new value kind states its own case, and the
	// job layer never grows a list of what it knows about.
	//
	// TYPE_REFFER and TYPE_CONST_REFFER are ALIASES, not things of their own — both
	// hop to the object they wrap and let IT answer. Without the hop the wrapper
	// would say "yes" on behalf of whatever it points at, which is exactly the
	// case that matters: a form, an object or a lambda is almost always reached
	// through one. Const-ness is not the question either — a read-only alias to a
	// mutable object still aliases a mutable object.
	virtual bool IsTransferable() const {
		if (m_pRef != nullptr && IsReference())
			return GetRef()->IsTransferable();
		return true;
	}

	virtual void SetType(ibValueTypes type);
	virtual ibValueTypes GetType() const;

	virtual bool IsEmpty() const;
	bool IsNull() const { return GetType() == ibValueTypes::TYPE_NULL; }

	// Its class — a primitive's by its kind, an object's its own (0 when it names none).
	virtual ibClassID GetClassType() const;

	virtual bool Init() {
		if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
			return m_pRef->Init();
		return true;
	}

	virtual void SetValue(const ibVariant& varValue);

	virtual bool SetBoolean(const ibString& strValue);
	virtual bool SetNumber(const ibString& strValue);
	virtual bool SetDate(const ibString& strValue);
	virtual bool SetString(const ibString& strValue);
	virtual bool SetString(ibString&& strValue);
	bool SetString(const char* sParam) { return SetString(ibString(sParam)); }
	bool SetString(const ibChar* sParam) { return SetString(ibString(sParam)); }

	void SetData(const ibVariant& varValue); //setting the value without changing the type

	virtual bool GetBoolean() const;
	virtual ibNumber GetNumber() const;
	virtual ibString GetString() const;
	virtual ibDateTime GetDate() const;

	int GetInteger() const { return GetNumber().ToInt(); }
	unsigned int GetUInteger() const { return GetNumber().ToUInt(); }
	double GetDouble() const { return GetNumber().ToDouble(); }

	// The primitives' types by their class ids, both ways — the core's table (core/core.h).
	static ibClassID GetIDByVT(const ibValueTypes& valueType);
	static ibValueTypes GetVTByID(const ibClassID& clsid);

	// A MEMBER OF AN ENUMERATION as a value — made by the side's enumeration, where T is complete.
	template <class T, typename valT = typename T::valEnumType>
	static ibVariant CreateEnumObject(const valT& v);

protected:

	// How an object writes itself into a node and reads itself back — its own.
	virtual bool DoSerialize(ibDataNode& node) const;
	virtual bool DoDeserialize(const ibDataNode& node);
};

#endif
