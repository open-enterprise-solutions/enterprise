#ifndef __VALUE_H__
#define __VALUE_H__

#include <memory>

#include "backend/backend_core.h"
#include "backend/compiler/typeCtor.h"

// Forward declaration - full definition in value_ptr.h included at end of file
template <class T> class ibValuePtr;

class ibValue;

// Cursor-based iteration state. Yielded by ibValue::CreateIterator()
// and driven by OPER_FOREACH / OPER_NEXT_ITER. Wrapped in shared_ptr
// (held by the runtime ibValueIterator), no manual delete.
class BACKEND_API ibValueIteratorState {
public:
	virtual ~ibValueIteratorState() = default;
	// Advance to the next element and copy it into `current`. Returns
	// false when the cursor is past the last element; the caller drops
	// the state. After Reset() the next MoveNext() yields element 0.
	virtual bool MoveNext(ibValue& current) = 0;
	virtual void Reset() = 0;

	// IntelliSense / editor type-hint. Writes a skeleton value of the
	// element type into `current` so the editor's static parser knows
	// what `x` is in `For Each x In container`. Returns false when no
	// typed skeleton is available; caller is expected to pre-initialise
	// `current` and treats it as untyped on a false return. Compile-
	// time only; not invoked at runtime by OPER_FOREACH.
	virtual bool PeekSample(ibValue& /*current*/) const { return false; }
};

extern BACKEND_API const ibValue wxEmptyValue;

// Forward declarations for template classes used in ConvertToEnumType/ConvertToEnumValue
template <typename valT> class ibValueEnumerationBase;
template <typename valT> class ibValueEnumerationVariantBase;
template <typename valT> class ibValueEnumeration;

// Forward declarations for CastValue (defined in value_cast.h, included at end of file)
template <typename T, typename U> static inline T* CastValue(U* ptr);
template <typename T, typename U> static inline T* CastValue(const U* ptr);
template <typename T, typename U> static inline T* CastValue(U& ptr);
template <typename T, typename U> static inline T* CastValue(const U& ptr);

class BACKEND_API ibBackendValue {
public:
	virtual ibValue* GetImplValueRef() const = 0;
};

const ibClassID g_valueUndefinedCLSID = string_to_clsid("VL_UNDF");

const ibClassID g_valueBooleanCLSID = string_to_clsid("VL_BOOL");
const ibClassID g_valueNumberCLSID = string_to_clsid("VL_NUMB");
const ibClassID g_valueDateCLSID = string_to_clsid("VL_DATE");
const ibClassID g_valueStringCLSID = string_to_clsid("VL_STRI");

const ibClassID g_valueNullCLSID = string_to_clsid("VL_NULL");

//simple type date
class BACKEND_API ibValue : public wxObject {
	wxDECLARE_DYNAMIC_CLASS(ibValue);
public:
	bool m_bReadOnly;
	//ATTRIBUTES:
	ibValueTypes m_typeClass;
	union {
		bool          m_bData;  //TYPE_BOOL
		wxLongLong_t  m_dData;  //TYPE_DATE
		ibValue*      m_pRef;   //TYPE_REFFER
	};
	wxString m_sData;  //TYPE_STRING
	// TYPE_NUMBER — outside the union. ibNumber is a conditional
	// heap-owner (lazy-grow BigImpl), so its ctor/dtor/operator= must
	// run on type transitions; in a union those calls are skipped and
	// the m_pRef pointer in the lower 4 bytes gets reinterpreted as a
	// BigImpl* on the next ibNumber::Clear(), which deletes random
	// memory. Same rule that already keeps wxString out of the union.
	// Costs +8 bytes per ibValue.
	ibNumber m_fData;
public:

	class BACKEND_API ibValueMethodHelper {

		//List of keywords that cannot be variable and function names
		struct ibValueMethodHelperConstructor {

			ibValueMethodHelperConstructor(const wxString& strHelper, const long paramCount, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_strHelper(strHelper), m_paramCount(paramCount), m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			wxString m_strHelper;
			long m_paramCount = 0;
			long m_lAlias, m_lData;
		};

		// Bit-flags for prop visibility / mutability.
		enum ibPropFlags : unsigned int {
			eProp_None     = 0,
			eProp_Readable = 1u << 0,
			eProp_Writable = 1u << 1,
			// Scope-local prop (ThisObject / ThisForm / similar): when
			// the host bc is mirrored to ibByteCode::m_listVar, this
			// flag travels into ibByteCodeVarInfo::m_bScoped, and the
			// cross-bc resolver (template FindVariable) skips the
			// entry. Children resolving `ThisObject` therefore can't
			// silently reach the parent's record.
			eProp_Scoped   = 1u << 2,
		};

		struct ibValueMethodHelperProperty {

			// Flag-based ctor — primary entry point. Use for new
			// callsites and any prop that needs eProp_Scoped or other
			// non-readable/writable bits.
			ibValueMethodHelperProperty(const wxString& strPropName, unsigned int flags, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strPropName), m_flags(flags), m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			// Legacy ctor — converts two bools (readable, writable)
			// into the flags word so existing AppendProp(bool, bool,
			// ...) overloads keep working without touching every
			// callsite. Old props default to non-scoped.
			ibValueMethodHelperProperty(const wxString& strPropName, bool readable, bool writable, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strPropName),
				  m_flags((readable ? eProp_Readable : 0u) | (writable ? eProp_Writable : 0u)),
				  m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			// 3-bool ctor — same as legacy plus an explicit `scoped`
			// argument. Readable for callsites that prefer bool args
			// over OR'd flag literals (ThisObject / ThisForm / etc).
			ibValueMethodHelperProperty(const wxString& strPropName, bool readable, bool writable, bool scoped, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strPropName),
				  m_flags((readable ? eProp_Readable : 0u) | (writable ? eProp_Writable : 0u) | (scoped ? eProp_Scoped : 0u)),
				  m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			bool IsReadable() const { return (m_flags & eProp_Readable) != 0; }
			bool IsWritable() const { return (m_flags & eProp_Writable) != 0; }
			bool IsScoped()   const { return (m_flags & eProp_Scoped)   != 0; }

			wxString m_fieldName;
			unsigned int m_flags = eProp_Readable | eProp_Writable;
			long m_lAlias, m_lData;
		};

		// Bit-flags for method capabilities — counterpart to ibPropFlags.
		enum ibMethodFlags : unsigned int {
			eMethod_None      = 0,
			eMethod_HasReturn = 1u << 0,   // function (returns) vs procedure (no return)
			eMethod_Scoped    = 1u << 1,   // bc-local — invisible to children
		};

		struct ibValueMethodHelperMethod {

			ibValueMethodHelperMethod(const wxString& strMethodName, const wxString& strHelper, const long paramCount, unsigned int flags, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strMethodName), m_strHelper(strHelper), m_paramCount(paramCount), m_flags(flags), m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			// Legacy ctor — convert hasRet bool into the flags word.
			ibValueMethodHelperMethod(const wxString& strMethodName, const wxString& strHelper, const long paramCount, bool hasRet, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strMethodName), m_strHelper(strHelper), m_paramCount(paramCount),
				  m_flags(hasRet ? eMethod_HasReturn : 0u), m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			bool HasReturn() const { return (m_flags & eMethod_HasReturn) != 0; }
			bool IsScoped()  const { return (m_flags & eMethod_Scoped)    != 0; }

			wxString m_fieldName;
			wxString m_strHelper;
			long m_paramCount = 0;
			unsigned int m_flags = 0;
			long m_lAlias, m_lData;
		};

		// constructors & props & methods
		std::vector<ibValueMethodHelperConstructor> m_constructorHelper; // tree of constructor names
		std::vector<ibValueMethodHelperProperty> m_propHelper; // tree of attribute names
		std::vector<ibValueMethodHelperMethod> m_methodHelper; // tree of method names

	public:

		ibValueMethodHelper() {}

		void ClearHelper() {
			m_constructorHelper.clear();
			m_propHelper.clear();
			m_methodHelper.clear();
		}

		inline long AppendConstructor(const wxString& strHelper) { return AppendConstructor(0, strHelper, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendConstructor(const wxString& strHelper, const long lCtorNum) { return AppendConstructor(0, strHelper, wxNOT_FOUND, lCtorNum); }
		inline long AppendConstructor(const long paramCount, const wxString& strHelper, const long lCtorNum) { return AppendConstructor(paramCount, strHelper, wxNOT_FOUND, lCtorNum); }
		inline long AppendConstructor(const long paramCount, const wxString& strHelper) { return AppendConstructor(paramCount, strHelper, wxNOT_FOUND, wxNOT_FOUND); }

		inline long AppendConstructor(const long paramCount, const wxString& strHelper, const long lCtorNum, const long lCtorAlias) {

			//auto iterator = std::find_if(m_constructorHelper.begin(), m_constructorHelper.end(),
			//	[strHelper](const auto& f) { return stringUtils::CompareString(f.m_strHelper, strHelper); });
			//if (iterator != m_constructorHelper.end())
			//	return std::distance(m_constructorHelper.begin(), iterator);

			m_constructorHelper.emplace_back(strHelper, paramCount, lCtorAlias, lCtorNum);
			return m_constructorHelper.size();
		}

		void CopyConstructor(const ibValueMethodHelper* src, const long lCtorNum) {
			if (lCtorNum < src->GetNConstructors()) {
				m_constructorHelper.push_back(src->m_constructorHelper[lCtorNum]);
			}
		}

		wxString GetConstructorHelper(const long lCtorNum) const {
			if (lCtorNum > GetNConstructors())
				return wxEmptyString;
			return m_constructorHelper[lCtorNum].m_strHelper;
		}

		long GetConstructorAlias(const long lCtorNum) const {
			if (lCtorNum > GetNConstructors())
				return wxNOT_FOUND;
			return m_constructorHelper[lCtorNum].m_lAlias;
		}

		long GetConstructorData(const long lCtorNum) const {
			if (lCtorNum > GetNConstructors())
				return wxNOT_FOUND;
			return m_constructorHelper[lCtorNum].m_lData;
		}

		const long int GetNConstructors() const noexcept { return m_constructorHelper.size(); }

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////

		inline long AppendProp(const wxString& strPropName) { return AppendProp(strPropName, true, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProp(const wxString& strPropName, const long lPropNum) { return AppendProp(strPropName, true, true, lPropNum, wxNOT_FOUND); }
		inline long AppendProp(const wxString& strPropName, const long lPropNum, const long lPropAlias) { return AppendProp(strPropName, true, true, lPropNum, lPropAlias); }
		inline long AppendProp(const wxString& strPropName, bool readable, const long lPropNum, const long lPropAlias) { return AppendProp(strPropName, readable, true, lPropNum, lPropAlias); }
		inline long AppendProp(const wxString& strPropName, bool readable, bool writable, const long lPropNum) { return AppendProp(strPropName, readable, writable, lPropNum, wxNOT_FOUND); }

		inline long AppendProp(const wxString& strPropName, bool readable, bool writable, const long lPropNum, const long lPropAlias) {
			const unsigned int flags = (readable ? eProp_Readable : 0u) | (writable ? eProp_Writable : 0u);
			return AppendProp(strPropName, flags, lPropNum, lPropAlias);
		}

		// 3-bool variant — same as legacy plus explicit `scoped`.
		// Out-of-line so the symbol is properly exported from
		// backend.dll for wfrontend / other consumers (otherwise MSVC
		// in Debug emits __imp_ stubs that the inline body can't
		// satisfy across the DLL boundary).
		long AppendProp(const wxString& strPropName, bool readable, bool writable, bool scoped, const long lPropNum, const long lPropAlias);

		// Flag-based variant. Use eProp_Readable | eProp_Writable
		// (or | eProp_Scoped for bc-local entries like ThisObject).
		inline long AppendProp(const wxString& strPropName, unsigned int flags, const long lPropNum, const long lPropAlias) {

			//auto iterator = std::find_if(m_propHelper.begin(), m_propHelper.end(),
			//	[strPropName](const auto& f) { return stringUtils::CompareString(f.m_fieldName, strPropName); });
			//if (iterator != m_propHelper.end())
			//	return std::distance(m_propHelper.begin(), iterator);

			m_propHelper.emplace_back(strPropName, flags, lPropAlias, lPropNum);
			return m_propHelper.size();
		}

		void CopyProp(const ibValueMethodHelper* src, const long lPropNum) {
			if (lPropNum < src->GetNProps()) {
				m_propHelper.push_back(src->m_propHelper[lPropNum]);
			}
		}

		void RemoveProp(const wxString& strPropName) {
			m_methodHelper.erase(
				std::remove_if(m_methodHelper.begin(), m_methodHelper.end(), [strPropName](const auto& f) {
					return stringUtils::CompareString(f.m_fieldName, strPropName); }), m_methodHelper.end());
		}

		long FindProp(const wxString& strPropName) const {
			auto iterator = std::find_if(m_propHelper.begin(), m_propHelper.end(),
				[strPropName](const auto& f) { return stringUtils::CompareString(f.m_fieldName, strPropName); }
			);
			if (iterator != m_propHelper.end())
				return std::distance(m_propHelper.begin(), iterator);
			return wxNOT_FOUND;
		}

		wxString GetPropName(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return wxEmptyString;
			return m_propHelper[lPropNum].m_fieldName;
		}

		long GetPropAlias(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return wxNOT_FOUND;
			return m_propHelper[lPropNum].m_lAlias;
		}

		long GetPropData(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return wxNOT_FOUND;
			return m_propHelper[lPropNum].m_lData;
		}

		virtual bool IsPropReadable(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return false;
			return m_propHelper[lPropNum].IsReadable();
		}

		virtual bool IsPropWritable(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return false;
			return m_propHelper[lPropNum].IsWritable();
		}

		// Scope-local props (ThisObject / ThisForm / similar) must
		// not leak across bc boundaries. Pass-3 PrepareModuleData
		// reads this and stamps ibCompileContext::ibVariable::m_bScoped
		// on the freshly pushed entry.
		virtual bool IsPropScoped(const long lPropNum) const {
			if (lPropNum > GetNProps())
				return false;
			return m_propHelper[lPropNum].IsScoped();
		}

		const long GetNProps() const noexcept { return m_propHelper.size(); }

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////

		inline long AppendProc(const wxString& strMethodName) { return AppendMethod(strMethodName, wxEmptyString, 0, false, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, 0, false, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, 0, false, lMethodNum, lMethodAlias); }
		inline long AppendProc(const wxString& strMethodName, const wxString& strHelper, const long paramCount, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, false, lMethodNum, lMethodAlias); }
		inline long AppendProc(const wxString& strMethodName, const long paramCount, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, paramCount, false, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodNum) { return AppendMethod(strMethodName, strHelper, paramCount, false, lMethodNum, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, false, lMethodNum, lMethodAlias); }

		inline long AppendFunc(const wxString& strMethodName) { return AppendMethod(strMethodName, wxEmptyString, 0, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendFunc(const wxString& strMethodName, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, 0, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendFunc(const wxString& strMethodName, const long paramCount, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, paramCount, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendFunc(const wxString& strMethodName, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, 0, true, lMethodNum, lMethodAlias); }
		inline long AppendFunc(const wxString& strMethodName, const wxString& strHelper, const long paramCount, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, true, lMethodNum, lMethodAlias); }
		inline long AppendFunc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, true, wxNOT_FOUND, lMethodAlias); }
		inline long AppendFunc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, true, lMethodNum, lMethodAlias); }

		inline long AppendMethod(const wxString& strMethodName, const long paramCount, bool hasRet, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, wxEmptyString, paramCount, hasRet, lMethodNum, lMethodAlias); }

		inline long AppendMethod(const wxString& strMethodName, const wxString& strHelper, const long paramCount, bool hasRet, const long lMethodNum, const long lMethodAlias) {

			//auto iterator = std::find_if(m_methodHelper.begin(), m_methodHelper.end(),
			//	[strMethodName](const auto& f) { return stringUtils::CompareString(f.m_fieldName, strMethodName); });
			//if (iterator != m_methodHelper.end())
			//	return std::distance(m_methodHelper.begin(), iterator);

			m_methodHelper.emplace_back(strMethodName, strHelper, paramCount, hasRet, lMethodAlias, lMethodNum);
			return m_methodHelper.size();
		}

		void CopyMethod(const ibValueMethodHelper* src, const long lMethodNum) {
			if (lMethodNum < src->GetNMethods()) {
				m_methodHelper.push_back(src->m_methodHelper[lMethodNum]);
			}
		}

		void RemoveMethod(const wxString& strMethodName) {
			m_methodHelper.erase(
				std::remove_if(m_methodHelper.begin(), m_methodHelper.end(), [strMethodName](const auto& f) {
					return stringUtils::CompareString(f.m_fieldName, strMethodName); }), m_methodHelper.end());
		}

		long FindMethod(const wxString& strMethodName) const {
			auto iterator = std::find_if(m_methodHelper.begin(), m_methodHelper.end(), [strMethodName](const auto& f) {
				return stringUtils::CompareString(f.m_fieldName, strMethodName); });

			if (iterator != m_methodHelper.end())
				return std::distance(m_methodHelper.begin(), iterator);
			return wxNOT_FOUND;
		}

		wxString GetMethodName(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxEmptyString;
			return m_methodHelper[lMethodNum].m_fieldName;
		}

		wxString GetMethodHelper(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxEmptyString;
			return m_methodHelper[lMethodNum].m_strHelper;
		}

		long GetMethodAlias(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxNOT_FOUND;
			return m_methodHelper[lMethodNum].m_lAlias;
		}

		// Read-only access to the full method/property/constructor lists.
		// Used by the help-corpus skeleton generator (tools/dumpHelp) to
		// enumerate every registered system function and property at
		// build time. Storage layout is stable for the process lifetime
		// (these vectors are populated once in PrepareNames and never
		// resized afterwards), so the references are safe for the
		// caller's iteration window.
		const std::vector<ibValueMethodHelperMethod>&      GetMethodList() const { return m_methodHelper; }
		const std::vector<ibValueMethodHelperProperty>&    GetPropList()   const { return m_propHelper; }
		const std::vector<ibValueMethodHelperConstructor>& GetCtorList()   const { return m_constructorHelper; }

		long GetMethodData(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxNOT_FOUND;
			return m_methodHelper[lMethodNum].m_lData;
		}

		bool HasRetVal(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return true;
			return m_methodHelper[lMethodNum].HasReturn();
		}

		// Scope-local method (bc-local — invisible to children
		// through cross-bc resolution). Symmetric with IsPropScoped.
		bool IsMethodScoped(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return false;
			return m_methodHelper[lMethodNum].IsScoped();
		}

		long GetNParams(const long lMethodNum) const {
			if (lMethodNum > GetNMethods())
				return wxNOT_FOUND;
			return m_methodHelper[lMethodNum].m_paramCount;
		}

		const long GetNMethods() const noexcept { return m_methodHelper.size(); }
	};

public:

	//METHODS:
	 //constructors:
	ibValue();
	ibValue(const ibValue& cParam);
	ibValue(ibValue&& cParam);
	ibValue(ibValue* pParam);
	ibValue(ibBackendValue* pParam);
	ibValue(ibValueTypes nType, bool readOnly = false);

	//copy constructors:
	ibValue(bool cParam); //boolean
	ibValue(signed int cParam); //number
	ibValue(unsigned int cParam); //number
	ibValue(double cParam); //number
	ibValue(const ibNumber& cParam); //number
	ibValue(wxLongLong_t cParam); //date
	ibValue(const wxDateTime& cParam); //date
	ibValue(int nYear, int nMonth, int nDay, unsigned short nHour = 0, unsigned short nMinute = 0, unsigned short nSecond = 0); //date

	ibValue(char* sParam); //string
	ibValue(wchar_t* sParam); //string
	ibValue(const wxStringImpl& sParam); //string
	ibValue(const wxString& sParam); //string

	//destructor:
	virtual ~ibValue();

	//clear values
	void Reset();

	//ref counter
	void IncrRef() { wxAtomicInc(m_refCount); }
	void DecrRef() {
		wxASSERT_MSG(m_refCount > 0, "invalid ref data count");
		if (!wxAtomicDec(m_refCount)) delete this;
	}

	//operators:
	void operator = (const ibValue& cParam);
	void operator = (ibValue&& cParam);

	void operator = (bool cParam);
	void operator = (short cParam);
	void operator = (unsigned short cParam);
	void operator = (int cParam);
	void operator = (unsigned int cParam);
	void operator = (float cParam);
	void operator = (double cParam);
	void operator = (const ibNumber& cParam);
	void operator = (const wxDateTime& cParam);
	void operator = (wxLongLong_t cParam);
	void operator = (const wxString& cParam);

	void operator = (ibValueTypes cParam);
	void operator = (ibBackendValue* pParam);
	void operator = (ibValue* pParam);

	//Implementation of comparison operators:
	bool operator > (const ibValue& cParam) const { return CompareValueGT(cParam); }
	bool operator >= (const ibValue& cParam) const { return CompareValueGE(cParam); }
	bool operator < (const ibValue& cParam) const { return CompareValueLS(cParam); }
	bool operator <= (const ibValue& cParam) const { return CompareValueLE(cParam); }
	bool operator == (const ibValue& cParam) const { return CompareValueEQ(cParam); }
	bool operator != (const ibValue& cParam) const { return CompareValueNE(cParam); }

	const ibValue& operator+(const ibValue& cParam);
	const ibValue& operator-(const ibValue& cParam);

	//Implementation of comparison operators:
	virtual bool CompareValueGT(const ibValue& cParam) const;
	virtual bool CompareValueGE(const ibValue& cParam) const;
	virtual bool CompareValueLS(const ibValue& cParam) const;
	virtual bool CompareValueLE(const ibValue& cParam) const;
	virtual bool CompareValueEQ(const ibValue& cParam) const;
	virtual bool CompareValueNE(const ibValue& cParam) const;

	//special converting
	template <typename valueType> inline valueType* ConvertToType() const {
		return CastValue<valueType>(this);
	}

	template <typename enumType> inline enumType ConvertToEnumType() const {
		ibValueEnumerationBase<enumType>* enumValue =
			CastValue<ibValueEnumerationBase<enumType>>(this);
		return enumValue ? enumValue->GetEnumValue() : enumType();
	}

	template <typename enumType> inline enumType ConvertToEnumValue() {
		ibValueEnumerationVariantBase<enumType>* enumValue =
			CastValue<ibValueEnumerationVariantBase<enumType>>(this);
		return enumValue ? enumValue->GetEnumValue() : enumType();
	}

	template <typename enumType > inline enumType ConvertToEnumValue() const {
		const ibValueEnumerationVariantBase<enumType>* enumValue =
			CastValue<ibValueEnumerationVariantBase<enumType>>(this);
		return enumValue ? enumValue->GetEnumValue() : enumType();
	}

	//convert to value
	template <typename T> inline bool ConvertToValue(T*& ptr) const {
		if (m_typeClass == ibValueTypes::TYPE_REFFER) {
			ibValue* non_const_value = GetRef();
			ptr = dynamic_cast<T*>(non_const_value);
			return ptr != nullptr;
		}
		else if (m_typeClass != ibValueTypes::TYPE_EMPTY) {
			ptr = dynamic_cast<T*>(const_cast<ibValue*>(this));
			return ptr != nullptr;
		}
		return false;
	}

public:

	//runtime support:
	template<typename T, typename... Args>
	static T* CreateAndPrepareValueRef(Args&&... args) {
		T* created_value = ::new T(std::forward<Args>(args)...);
		if (created_value == nullptr)
			return nullptr;
		created_value->PrepareNames();
		return created_value;
	}

	template<typename T>
	static ibValue CreateObject(ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef<T>(paParams, lSizeArray);
	}
	static ibValue CreateObject(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	static ibValue CreateObject(const wxClassInfo* classInfo, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(classInfo, paParams, lSizeArray);
	}
	static ibValue CreateObject(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(className, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	static ibValue CreateObjectValue(Args&&... args) {
		return CreateObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	static ibValue* CreateObjectRef(ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(CLASSINFO(T), paParams, lSizeArray);
	}
	static ibValue* CreateObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0);
	static ibValue* CreateObjectRef(const wxClassInfo* classInfo, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		const ibClassID& clsid = GetTypeIDByRef(classInfo);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	static ibValue* CreateObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		const ibClassID& clsid = GetIDObjectFromString(className);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	static ibValue* CreateObjectValueRef(Args&&... args) {
		return CreateAndConvertObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	static T* CreateAndConvertObjectRef(ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(CLASSINFO(T), paParams, lSizeArray));
	}
	template<class T = ibValue>
	static T* CreateAndConvertObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(clsid, paParams, lSizeArray));
	}
	template<class T = ibValue>
	static T* CreateAndConvertObjectRef(const wxClassInfo* classInfo, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(classInfo, paParams, lSizeArray));
	}
	template<class T = ibValue>
	static T* CreateAndConvertObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(className, paParams, lSizeArray));
	}
	template<typename T, typename... Args>
	static T* CreateAndConvertObjectValueRef(Args&&... args) {
		const ibClassID& clsid = ibValue::GetTypeIDByRef(CLASSINFO(T));
		if (ibValue::IsRegisterCtor(clsid))
			return CreateAndPrepareValueRef<T>(args...);
		wxASSERT_MSG(false, "CreateAndConvertObjectValueRef ret null!");
		return nullptr;
	}

	template<typename T, typename valT>
	static ibValue CreateEnumObject(const valT& v) {
		return CreateEnumObjectRef<T>(v);
	}

	template<typename T, typename valT>
	static ibValue* CreateEnumObjectRef(const valT& v) {
		return CreateAndConvertEnumObjectRef<T>(v);
	}

	template<typename T, typename valT>
	static ibValue* CreateAndConvertEnumObjectRef(const valT& v);

	static void RegisterCtor(ibCtorAbstractType* typeCtor);
	static void UnRegisterCtor(ibCtorAbstractType*& typeCtor);
	static void UnRegisterCtor(const wxString& className);

	static bool IsRegisterCtor(const wxString& className);
	static bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType);
	static bool IsRegisterCtor(const ibClassID& clsid);

	static ibClassID GetTypeIDByRef(const wxClassInfo* classInfo);
	static ibClassID GetTypeIDByRef(const ibValue* objectRef);

	static ibClassID GetIDObjectFromString(const wxString& className);
	static bool CompareObjectName(const wxString& className, ibValueTypes valueType) {
		return stringUtils::CompareString(className, GetNameObjectFromVT(valueType));
	}

	static wxString GetNameObjectFromID(const ibClassID& clsid, bool upper = false);
	static wxString GetNameObjectFromVT(ibValueTypes valueType, bool upper = false);
	static ibValueTypes GetVTByID(const ibClassID& clsid);
	static ibClassID GetIDByVT(const ibValueTypes& valueType);

	static ibCtorAbstractType* GetAvailableCtor(const wxString& className);
	static ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid);
	static ibCtorAbstractType* GetAvailableCtor(const wxClassInfo* classInfo);

	static std::vector<ibCtorAbstractType*> GetListCtorsByType(ibCtorObjectType objectType = ibCtorObjectType::ibCtorObjectType_object_value);

	//static event 
	static void OnRegisterObject(const wxString& className, ibCtorAbstractType* typeCtor) {}
	static void OnUnRegisterObject(const wxString& className) {}

	//factory version 
	static unsigned int GetFactoryCountChanges();

public:

	//special copy & move function
	inline void Copy(const ibValue& cOld);
	inline void Move(ibValue&& cOld);

	void FromDate(int& nYear, int& nMonth, int& nDay) const;
	void FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const;
	void FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const;

#pragma region serialization
	bool Serialize(wxString& strValue) const { return DoSerialize(strValue); }
	bool Deserialize(const wxString& strValue) { return DoDeserialize(strValue); }
#pragma endregion

	//Virtual methods:
	virtual void SetType(ibValueTypes type);
	virtual ibValueTypes GetType() const;

	virtual bool IsEmpty() const;

	virtual wxString GetClassName() const;
	virtual ibClassID GetClassType() const;

	virtual bool Init() {
		if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
			return m_pRef->Init();
		return true;
	}

	virtual bool Init(ibValue** paParams, const long lSizeArray) {
		if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
			return m_pRef->Init(paParams, lSizeArray);
		return true;
	}

	virtual void SetValue(const ibValue& varValue);

	virtual bool SetBoolean(const wxString& strValue);
	virtual bool SetNumber(const wxString& strValue);
	virtual bool SetDate(const wxString& strValue);
	virtual bool SetString(const wxString& strValue);

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const;

	void SetData(const ibValue& varValue); //setting the value without changing the type

	virtual ibValue GetValue(bool getThis = false) const;

	// Produce a fresh, independent copy of the value. For simple types
	// (Boolean / Number / String / Date / Null / Empty) the data is
	// value-copied — caller can mutate without touching the source.
	// Aggregate / reference types must override to provide their own
	// clone semantics; the base default returns an undefined value to
	// surface "tried to clone something that doesn't support it" as
	// an empty result rather than a silent share.
	virtual ibValue Clone() const {
		switch (m_typeClass) {
		case ibValueTypes::TYPE_EMPTY:
		case ibValueTypes::TYPE_NULL:
		case ibValueTypes::TYPE_BOOLEAN:
		case ibValueTypes::TYPE_NUMBER:
		case ibValueTypes::TYPE_STRING:
		case ibValueTypes::TYPE_DATE:
			return *this;  // value-copy of the simple-type payload
		default:
			return ibValue();  // no own clone — undefined
		}
	}

	virtual bool GetBoolean() const;
	virtual int GetInteger() const { return GetNumber().ToInt(); }
	virtual unsigned int GetUInteger() const { return GetNumber().ToUInt(); }
	virtual double GetDouble() const { return GetNumber().ToDouble(); }
	virtual wxDateTime GetDateTime() const { return wxLongLong(GetDate()); }

	virtual ibNumber GetNumber() const;
	virtual wxString GetString() const;
	virtual wxLongLong_t GetDate() const;

	/////////////////////////////////////////////////////////////////////////

	virtual ibValue* GetRef() const;

	/////////////////////////////////////////////////////////////////////////

	virtual void ShowValue();

	/////////////////////////////////////////////////////////////////////////

#pragma region attribute_support

	virtual ibValueMethodHelper* GetPMethods() const {
		return m_typeClass == ibValueTypes::TYPE_REFFER && m_pRef != nullptr ?
			m_pRef->GetPMethods() : nullptr;
	}

	//collect 
	virtual void PrepareNames() const;

	/// Returns number of component properties
	/**
	*  @return number of properties
	*/
	virtual long GetNProps() const;

	/// Finds property by name
	/**
	 *  @param wsPropName - property name
	 *  @return property index or -1, if iterator is not found
	 */
	virtual long FindProp(const wxString& strPropName) const;

	/// Returns property name
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return proeprty name or 0 if iterator is not found
	 */
	virtual wxString GetPropName(const long lPropNum) const;

	/// Returns property value
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return the result of
	 */
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	/// Sets the property value
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @param varPropVal - the pointer to a variable for property value
	 *  @return the result of
	 */
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	/// Is property readable?
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return true if property is readable
	 */
	virtual bool IsPropReadable(const long lPropNum) const;

	/// Is property writable?
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return true if property is writable
	 */
	virtual bool IsPropWritable(const long lPropNum) const;

	/// Is property scope-local (bc-local — invisible to children
	/// through cross-bc resolution; ThisObject / ThisForm / etc.)?
	virtual bool IsPropScoped(const long lPropNum) const;

	/// Returns number of component methods
	/**
	 *  @return number of component  methods
	 */
	virtual long GetNMethods() const;

	/// Finds a method by name 
	/**
	 *  @param wsMethodName - method name
	 *  @return - method index
	 */
	virtual long FindMethod(const wxString& strMethodName) const;

	/// Returns method name
	/**
	 *  @param lMethodNum - method index(starting with 0)
	 *  @return method name or 0 if method is not found
	 */
	virtual wxString GetMethodName(const long lMethodNum) const;

	/// Returns method helper
	/**
	*  @param lMethodNum - method index(starting with 0)
	*  @return method name or 0 if method is not found
	*/
	virtual wxString GetMethodHelper(const long lMethodNum) const;

	/// Returns number of method parameters
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @return number of parameters
	 */
	virtual long GetNParams(const long lMethodNum) const;

	/// Returns default value of method parameter
	/**
	 *  @param lMethodNum - method index(starting with 0)
	 *  @param lParamNum - parameter index (starting with 0)
	 *  @param pvarParamDefValue - the pointer to a variable for default value
	 *  @return the result of
	 */
	virtual bool GetParamDefValue(const long lMethodNum,
		const long lParamNum,
		ibValue& pvarParamDefValue) const;

	/// Does the method have a return value?
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @return true if the method has a return value
	 */
	virtual bool HasRetVal(const long lMethodNum) const;

	// LINQ — universal pipeline ops (Where / Select / OrderBy / ...) on
	// every iterable value. Resolved at compile time by name via
	// FindLinqMethodByName and emitted as OPER_CALL_LINQ (NOT
	// OPER_CALL_METHOD); the opcode carries the ibLinqMethod enum id
	// directly. Runtime dispatch goes through virtual DispatchLinqMethod —
	// derived classes don't have to teach FindMethod / CallAsFunc about
	// LINQ surface.

	// LINQ method id — strongly-typed enum. Single source of truth
	// for method-name resolution: name-string == enum-value's
	// stringization (`Where` enumerator ↔ `"Where"` script-side
	// method name). Adding a new op = append here AND an entry in
	// GetLinqMethodTable() + a case in the dispatch switch
	// (procUnitLinq.cpp). Runtime arg-count validation lives inside
	// each dispatch case.
	enum class ibLinqMethod : long {
		Where,
		Select,
		Count,
		ToArray,
		First,
		Any,
		Distinct,
		OrderBy,
		OrderByDescending,
		GroupBy,
		Join,
		Skip,
		Take,
		SkipWhile,
		TakeWhile,
		Reverse,
		Concat,
		Union,
		Intersect,
		Except,
		Last,
		LastOrDefault,
		Single,
		SingleOrDefault,
		FirstOrDefault,
		ElementAt,
		ElementAtOrDefault,
		Contains,
		SequenceEqual,
		Aggregate,
		WhereIndexed,
		SelectIndexed,
	};

	// Method-table entry — enum id + script-side name + one-line help
	// for IntelliSense tooltips. Same table services compile-side
	// FindLinqMethodByName (name → enum) and frontend autocomplete
	// (after `.`, append every LINQ method as a suggestion). Single
	// source of truth: no separate IB_LINQ_TRY macro per name in the
	// resolver, no parallel list in the autocomplete loader.
	struct ibLinqMethodInfo {
		ibLinqMethod   id;
		const wchar_t* name;
		const wchar_t* helper;
	};
	static const std::vector<ibLinqMethodInfo>& GetLinqMethodTable();

	// Compile-time resolver — script method name → enum value (or
	// -1 if not a LINQ method). Used by the chain-method emit path
	// (compileCode.cpp) to decide OPER_CALL_METHOD (per-class) vs
	// OPER_CALL_LINQ (universal pipeline op). Case-insensitive match
	// per OES convention. Implemented in terms of GetLinqMethodTable().
	static long FindLinqMethodByName(const wxString& strMethodName);

	// LINQ dispatch entry point — virtual so subclasses can override
	// kind-specific behavior (e.g. ibValueQuery extending an existing
	// pipeline rather than wrapping via CreateIterator + new state;
	// ibValueArray returning concrete Array on ToArray() rather than
	// materialising into a fresh one; DB-backed values pushing
	// OrderBy / GroupBy / Join down to SQL). Default impl walks
	// TYPE_REFFER chain to the underlying object, then dispatches
	// through the central state-class machinery (file-scope helper
	// in procUnit.cpp). One virtual entry rather than 32 keeps
	// vtable bloat down — per-method virtuals can be lifted later
	// when concrete override pressure surfaces (Phase 2 of the
	// virtual-dispatch arc).
	virtual void DispatchLinqMethod(ibLinqMethod method, ibValue& ret,
	                                ibValue** args, long n);

	/// Calls the method as a procedure
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @param paParams - the pointer to array of method parameters
	 *  @param lSizeArray - the size of array
	 *  @return the result of
	 */
	virtual bool CallAsProc(const long lMethodNum,
		ibValue** paParams,
		const long lSizeArray);

	/// Calls the method as a function
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @param pvarRetValue - the pointer to returned value
	 *  @param paParams - the pointer to array of method parameters
	 *  @param lSizeArray - the size of array
	 *  @return the result of
	 */
	virtual bool CallAsFunc(const long lMethodNum,
		ibValue& pvarRetValue,
		ibValue** paParams,
		const long lSizeArray);

public:

	ibValue* GetThis() { return this; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	ibValue operator[](const ibValue& varKeyValue) {
		ibValue retValue;
		GetAt(varKeyValue, retValue);
		return retValue;
	}
#pragma region iterator_support
	// Cursor-based iteration. Returns a state walked by OPER_FOREACH /
	// OPER_NEXT_ITER; null means "this value isn't iterable" —
	// OPER_FOREACH raises a runtime error in that case. shared_ptr
	// matches the project convention for owned heap pointers.
	//
	// Base default delegates through the TYPE_REFFER chain to the
	// underlying object; every container class supporting script-level
	// `For Each` overrides this with its own walker. The state's
	// PeekSample() doubles as the IntelliSense type hint, so there's
	// no separate "empty element" surface on ibValue.
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator();
#pragma endregion

protected:

#pragma region serialization
	virtual bool DoSerialize(wxString& strValue) const;
	virtual bool DoDeserialize(const wxString& strValue);
#pragma endregion

private:

	unsigned int m_refCount;
};
#include "backend/value_ptr.h"
#include "backend/value_cast.h"

/////////////////////////////////////////////////////////////////////////
// ibValue template implementations deferred until ibValuePtr is complete
/////////////////////////////////////////////////////////////////////////

template<typename T, typename valT>
ibValue* ibValue::CreateAndConvertEnumObjectRef(const valT& v) {
	ibValuePtr<ibValueEnumeration<valT>> createdEnum(ibValue::CreateAndConvertObjectRef<T>());
	wxASSERT(createdEnum != nullptr);
	return createdEnum->CreateEnumVariantValue(v);
}

/////////////////////////////////////////////////////////////////////////
// value_register template implementations (deferred from typeCtor.h
// because they require the complete ibValue type)
/////////////////////////////////////////////////////////////////////////

template<typename typeCtor>
value_register<typeCtor>::value_register(typeCtor* so) : m_so(so) {
	try {
		if (m_so != nullptr) {
			ibValue::RegisterCtor(m_so);
		}
	}
	catch (...) {
#ifdef DEBUG
		wxLogDebug(wxT("! failed to register class: %s"), m_so->GetClassName());
#endif
		wxDELETE(m_so);
	}
}

template<typename typeCtor>
value_register<typeCtor>::~value_register() {
	try {
		if (m_so != nullptr) {
			ibValue::UnRegisterCtor(m_so);
		}
	}
	catch (...) {
#ifdef DEBUG
		wxLogDebug(wxT("! failed to unregister class: %s"), m_so->GetClassName());
#endif
		wxDELETE(m_so);
	}
}

#endif