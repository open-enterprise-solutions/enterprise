////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, Vadim Zeitlin
//	Description : OLE-supporter
////////////////////////////////////////////////////////////////////////////

#include "valueOLE.h"
#include "valueArray.h"
#include "backend/backend_exception.h"
#include "backend/compiler/procUnit.h"          // InvokeLambda — an event handler is a lambda
#include "backend/compiler/procUnitLambda.h"    // AsFunction

#ifdef __WXMSW__
#include <ocidl.h>      // IConnectionPointContainer, IProvideClassInfo
#include <wx/log.h>     // wxSysErrorMsgStr

#include <algorithm>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#endif

#pragma warning(push)
#pragma warning(disable : 4018)

// The object's own two members, beside whatever the COM object has. Non-negative, because the interpreter
// reads a negative member number as "not found"; at the top of the range, where neither a type library
// (0x6000xxxx) nor a hand-written dispatch numbers its members.
static constexpr long s_methodAddEventHandler = 0x7FFFFF01;
static constexpr long s_methodRemoveEventHandler = 0x7FFFFF02;

// A COM method's arity is not known without its type information, and a call may pass any number of
// arguments: the interpreter sizes the frame by this bound, and refuses a call that passes more.
static constexpr long s_comArgsBound = 64;

#ifdef __WXMSW__
static std::map<IDispatch*, ibValueOLE*> gs_valueOLE;

//*********************************************************************************************************************
//*                                                      Apartment                                                    *
//*********************************************************************************************************************

// ⭐ A THREAD ENTERS A COM APARTMENT BEFORE IT TOUCHES AN OBJECT. The thin client's main thread is in one
// already (wx initialises OLE), but a session on the application server runs on a worker thread that never
// asked: CoCreateInstance there answered CO_E_NOTINITIALIZED. A single-threaded apartment, so an object
// and its events stay on the thread that made them; balanced when the thread ends. A thread that is in the
// multithreaded apartment already (the debugger's) keeps it — RPC_E_CHANGED_MODE is not ours to undo.
static void EnterComApartment()
{
	struct ibComApartment {
		const HRESULT m_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		~ibComApartment() { if (SUCCEEDED(m_hr)) ::CoUninitialize(); }
	};
	thread_local ibComApartment s_apartment;
	(void)s_apartment;
}

//*********************************************************************************************************************
//*                                                      Strings                                                      *
//*********************************************************************************************************************

static BSTR ToBSTR(const ibString& str)
{
	return ::SysAllocStringLen(str.wc_str(), static_cast<UINT>(str.length()));
}

// A null BSTR is an empty string — the convention every automation server follows.
static ibString FromBSTR(const BSTR bstr)
{
	if (bstr == nullptr)
		return ibString();
	return ibString(bstr, ::SysStringLen(bstr));
}

static ibString HResultText(const HRESULT hr)
{
	return ibString(wxSysErrorMsgStr(static_cast<unsigned long>(hr)));
}

//*********************************************************************************************************************
//*                                                      Conversion                                                   *
//*********************************************************************************************************************

static ibValue FromSafeArray(SAFEARRAY* psa, const VARTYPE elementType, const ibString& objectName);

static ibString InterfaceName(IDispatch* dispatch, const ibString& fallback)
{
	ITypeInfo* typeInfo = nullptr;
	if (FAILED(dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)) || typeInfo == nullptr)
		return fallback;
	BSTR name = nullptr;
	ibString result = fallback;
	if (SUCCEEDED(typeInfo->GetDocumentation(MEMBERID_NIL, &name, nullptr, nullptr, nullptr)) && name != nullptr)
		result = FromBSTR(name);
	::SysFreeString(name);
	typeInfo->Release();
	return result;
}

void ibValueOLE::FromVariant(const VARIANT& oleVariant, ibValue& value, const ibString& objectName)
{
	// BY REFERENCE: read what it points at — VariantCopyInd takes every kind of reference apart.
	if ((oleVariant.vt & VT_BYREF) != 0) {
		VARIANT direct;
		::VariantInit(&direct);
		const HRESULT hr = ::VariantCopyInd(&direct, const_cast<VARIANT*>(&oleVariant));
		if (FAILED(hr))
			ibBackendCoreException::Error(_("COM object \"%s\" returned a value that cannot be read: %s"), objectName, HResultText(hr));
		try {
			FromVariant(direct, value, objectName);
		}
		catch (...) {
			::VariantClear(&direct);
			throw;
		}
		::VariantClear(&direct);
		return;
	}

	if ((oleVariant.vt & VT_ARRAY) != 0) {
		value = FromSafeArray(oleVariant.parray, oleVariant.vt & VT_TYPEMASK, objectName);
		return;
	}

	switch (oleVariant.vt)
	{
	case VT_EMPTY:
	case VT_ERROR:      // a missing value: DISP_E_PARAMNOTFOUND is how automation spells "omitted"
		value.Reset();
		return;
	case VT_NULL:
		value.SetType(ibValueTypes::TYPE_NULL);
		return;
	case VT_BOOL:
		value = ibValue(oleVariant.boolVal != VARIANT_FALSE);
		return;
	case VT_I1:
		value = ibValue(ibNumber(static_cast<int>(oleVariant.cVal)));
		return;
	case VT_UI1:
		value = ibValue(ibNumber(static_cast<unsigned int>(oleVariant.bVal)));
		return;
	case VT_I2:
		value = ibValue(ibNumber(static_cast<int>(oleVariant.iVal)));
		return;
	case VT_UI2:
		value = ibValue(ibNumber(static_cast<unsigned int>(oleVariant.uiVal)));
		return;
	case VT_I4:
		value = ibValue(ibNumber(static_cast<long>(oleVariant.lVal)));
		return;
	case VT_UI4:
		value = ibValue(ibNumber(static_cast<unsigned long>(oleVariant.ulVal)));
		return;
	case VT_INT:
		value = ibValue(ibNumber(static_cast<int>(oleVariant.intVal)));
		return;
	case VT_UINT:
		value = ibValue(ibNumber(static_cast<unsigned int>(oleVariant.uintVal)));
		return;
	case VT_I8:
		value = ibValue(ibNumber(static_cast<long long>(oleVariant.llVal)));
		return;
	case VT_UI8:
		value = ibValue(ibNumber(static_cast<unsigned long long>(oleVariant.ullVal)));
		return;
	case VT_R4:
		value = ibValue(ibNumber(static_cast<double>(oleVariant.fltVal)));
		return;
	case VT_R8:
		value = ibValue(ibNumber(oleVariant.dblVal));
		return;
	case VT_CY:
		// Currency is an integer of ten-thousandths: divided exactly, never through a double.
		value = ibValue(ibNumber(static_cast<long long>(oleVariant.cyVal.int64)) / ibNumber(10000));
		return;
	case VT_DECIMAL: {
		// 96 bits and a scale — written out in the invariant locale and read back exactly.
		BSTR text = nullptr;
		const HRESULT hr = ::VarBstrFromDec(const_cast<DECIMAL*>(&oleVariant.decVal), LOCALE_INVARIANT, 0, &text);
		if (FAILED(hr))
			ibBackendCoreException::Error(_("COM object \"%s\" returned a value that cannot be read: %s"), objectName, HResultText(hr));
		const ibString digits = FromBSTR(text);
		::SysFreeString(text);
		value = ibValue(ibNumber(digits));
		return;
	}
	case VT_BSTR:
		value = ibValue(FromBSTR(oleVariant.bstrVal));
		return;
	case VT_DATE: {
		// A VT_DATE is a wall-clock reading with no zone in it, exactly as the engine's date is
		// (fdatetime.h): the parts cross over as they are, nothing asks the machine's clock.
		SYSTEMTIME st = {};
		if (!::VariantTimeToSystemTime(oleVariant.date, &st))
			ibBackendCoreException::Error(_("COM object \"%s\" returned a date that cannot be read"), objectName);
		value = ibValue(ibDateTime(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds));
		return;
	}
	case VT_DISPATCH:
		if (oleVariant.pdispVal == nullptr)
			value.Reset();
		else
			value = new ibValueOLE(CLSID_NULL, oleVariant.pdispVal, InterfaceName(oleVariant.pdispVal, objectName));
		return;
	case VT_UNKNOWN: {
		// An interface that answers IDispatch is an object like any other; one that does not cannot be
		// called by name, and saying so is better than handing back an Undefined that hides it.
		if (oleVariant.punkVal == nullptr) {
			value.Reset();
			return;
		}
		IDispatch* dispatch = nullptr;
		if (FAILED(oleVariant.punkVal->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&dispatch))) || dispatch == nullptr)
			ibBackendCoreException::Error(_("COM object \"%s\" returned an interface that cannot be called by name"), objectName);
		value = new ibValueOLE(CLSID_NULL, dispatch, InterfaceName(dispatch, objectName));
		dispatch->Release();
		return;
	}
	}

	ibBackendCoreException::Error(_("COM object \"%s\" returned a value of a kind the language does not have (VARTYPE %d)"),
		objectName, static_cast<int>(oleVariant.vt));
}

// One element of an array, read into a VARIANT of its own whatever the array holds. The element's storage
// is the VARIANT's union (8 bytes in) for every kind but DECIMAL, which takes the whole VARIANT.
static void ReadSafeArrayElement(SAFEARRAY* psa, LONG* indices, const VARTYPE elementType, VARIANT& element, const ibString& objectName)
{
	::VariantInit(&element);
	HRESULT hr = S_OK;
	if (elementType == VT_VARIANT) {
		hr = ::SafeArrayGetElement(psa, indices, &element);
	}
	else if (elementType == VT_DECIMAL) {
		DECIMAL dec = {};
		hr = ::SafeArrayGetElement(psa, indices, &dec);
		element.decVal = dec;
		element.vt = VT_DECIMAL;      // after the copy: a DECIMAL's first word is where vt lives
	}
	else if (elementType == VT_RECORD) {
		ibBackendCoreException::Error(_("COM object \"%s\" returned an array of structures, which the language does not have"), objectName);
	}
	else {
		hr = ::SafeArrayGetElement(psa, indices, &element.llVal);
		element.vt = elementType;
	}
	if (FAILED(hr))
		ibBackendCoreException::Error(_("COM object \"%s\" returned a value that cannot be read: %s"), objectName, HResultText(hr));
}

// Dimension `dim` (1 = the left-most, the outer array) of a SAFEARRAY. SafeArrayGetElement counts its
// indices the other way round — the right-most dimension is indices[0] — hence `dims - dim`.
static ibValue FromSafeArrayDimension(SAFEARRAY* psa, const VARTYPE elementType, const UINT dim, const UINT dims,
	std::vector<LONG>& indices, const ibString& objectName)
{
	LONG lower = 0, upper = -1;
	::SafeArrayGetLBound(psa, dim, &lower);
	::SafeArrayGetUBound(psa, dim, &upper);

	ibValueArray* const arr = new ibValueArray();
	ibValue result = arr;
	for (LONG i = lower; i <= upper; i++) {
		indices[dims - dim] = i;
		if (dim < dims) {
			arr->Add(FromSafeArrayDimension(psa, elementType, dim + 1, dims, indices, objectName));
			continue;
		}
		VARIANT element;
		ReadSafeArrayElement(psa, indices.data(), elementType, element, objectName);
		ibValue item;
		try {
			ibValueOLE::FromVariant(element, item, objectName);
		}
		catch (...) {
			::VariantClear(&element);
			throw;
		}
		::VariantClear(&element);
		arr->Add(item);
	}
	return result;
}

static ibValue FromSafeArray(SAFEARRAY* psa, const VARTYPE elementType, const ibString& objectName)
{
	if (psa == nullptr)
		return ibValue(new ibValueArray());
	const UINT dims = ::SafeArrayGetDim(psa);
	if (dims == 0)
		return ibValue(new ibValueArray());
	std::vector<LONG> indices(dims, 0);
	return FromSafeArrayDimension(psa, elementType, 1, dims, indices, objectName);
}

void ibValueOLE::ToVariant(const ibValue& value, VARIANT& oleVariant)
{
	::VariantInit(&oleVariant);

	switch (value.GetType())
	{
	case ibValueTypes::TYPE_EMPTY:
		oleVariant.vt = VT_EMPTY;
		return;
	case ibValueTypes::TYPE_NULL:
		oleVariant.vt = VT_NULL;
		return;
	case ibValueTypes::TYPE_BOOLEAN:
		oleVariant.vt = VT_BOOL;
		oleVariant.boolVal = value.GetBoolean() ? VARIANT_TRUE : VARIANT_FALSE;
		return;
	case ibValueTypes::TYPE_NUMBER: {
		// An integer goes as the integer automation reads everywhere (VT_I4), a larger one as VT_I8, and
		// a fraction as a double — an old server refuses VT_I8 outright, and every one reads VT_R8.
		const ibNumber number = value.GetNumber();
		long long integer = 0;
		if (number == number.Round() && number.ToInt(integer) == 0) {
			if (integer >= INT32_MIN && integer <= INT32_MAX) {
				oleVariant.vt = VT_I4;
				oleVariant.lVal = static_cast<LONG>(integer);
			}
			else {
				oleVariant.vt = VT_I8;
				oleVariant.llVal = integer;
			}
			return;
		}
		oleVariant.vt = VT_R8;
		oleVariant.dblVal = number.ToDouble();
		return;
	}
	case ibValueTypes::TYPE_STRING:
		oleVariant.vt = VT_BSTR;
		oleVariant.bstrVal = ToBSTR(value.GetString());
		return;
	case ibValueTypes::TYPE_DATE: {
		ibDateTimeParts p;
		value.GetDate().ToParts(p);
		SYSTEMTIME st = {};
		st.wYear = static_cast<WORD>(p.m_year);
		st.wMonth = static_cast<WORD>(p.m_month);
		st.wDay = static_cast<WORD>(p.m_day);
		st.wDayOfWeek = static_cast<WORD>(p.m_weekDay % 7);   // SYSTEMTIME counts Sunday as 0
		st.wHour = static_cast<WORD>(p.m_hour);
		st.wMinute = static_cast<WORD>(p.m_minute);
		st.wSecond = static_cast<WORD>(p.m_second);
		st.wMilliseconds = static_cast<WORD>(p.m_millisecond);
		// The empty date (0001-01-01) is before the first day automation can count (year 100): it goes
		// as the empty value it means, the way an omitted date does.
		if (value.GetDate() == ibDateTime()) {
			oleVariant.vt = VT_EMPTY;
			return;
		}
		if (!::SystemTimeToVariantTime(&st, &oleVariant.date))
			ibBackendCoreException::Error(_("The date %s cannot be passed to a COM object"), value.GetString());
		oleVariant.vt = VT_DATE;
		return;
	}
	default:
		break;
	}

	ibValueOLE* valueOLE = nullptr;
	if (value.ConvertToValue(valueOLE) && valueOLE != nullptr && valueOLE->m_dispatch != nullptr) {
		oleVariant.vt = VT_DISPATCH;
		oleVariant.pdispVal = valueOLE->m_dispatch;
		oleVariant.pdispVal->AddRef();
		return;
	}

	ibValueArray* valueArray = nullptr;
	if (value.ConvertToValue(valueArray) && valueArray != nullptr) {
		const std::vector<ibValue>& items = valueArray->Values();
		SAFEARRAY* psa = ::SafeArrayCreateVector(VT_VARIANT, 0, static_cast<ULONG>(items.size()));
		if (psa == nullptr)
			ibBackendCoreException::Error(_("An array of %d elements cannot be passed to a COM object"), static_cast<int>(items.size()));
		for (LONG i = 0; i < static_cast<LONG>(items.size()); i++) {
			VARIANT element;
			try {
				ToVariant(items[i], element);
			}
			catch (...) {
				::SafeArrayDestroy(psa);
				throw;
			}
			::SafeArrayPutElement(psa, &i, &element);   // copies it
			::VariantClear(&element);
		}
		oleVariant.vt = VT_ARRAY | VT_VARIANT;
		oleVariant.parray = psa;
		return;
	}

	ibBackendCoreException::Error(_("A value of type \"%s\" cannot be passed to a COM object"), value.GetClassName());
}

// The value a handler or a by-reference argument left, written where the VARIANT points.
static void WriteByRef(VARIANT& target, const ibValue& value)
{
	const VARTYPE vt = target.vt & ~VT_BYREF;
	if (vt == VT_VARIANT) {
		::VariantClear(target.pvarVal);
		ibValueOLE::ToVariant(value, *target.pvarVal);
		return;
	}

	VARIANT source, typed;
	ibValueOLE::ToVariant(value, source);
	::VariantInit(&typed);
	const HRESULT hr = ::VariantChangeType(&typed, &source, 0, vt);
	::VariantClear(&source);
	if (FAILED(hr))
		return;     // the kind the caller declared cannot hold it: the argument keeps what it had

	switch (vt)
	{
	case VT_BSTR:
		::SysFreeString(*target.pbstrVal);
		*target.pbstrVal = typed.bstrVal;      // the string moves over, not copied
		return;
	case VT_DISPATCH:
		if (*target.ppdispVal != nullptr)
			(*target.ppdispVal)->Release();
		*target.ppdispVal = typed.pdispVal;
		return;
	case VT_BOOL:   *target.pboolVal = typed.boolVal; break;
	case VT_I1:     *target.pcVal = typed.cVal; break;
	case VT_UI1:    *target.pbVal = typed.bVal; break;
	case VT_I2:     *target.piVal = typed.iVal; break;
	case VT_UI2:    *target.puiVal = typed.uiVal; break;
	case VT_I4:     *target.plVal = typed.lVal; break;
	case VT_UI4:    *target.pulVal = typed.ulVal; break;
	case VT_INT:    *target.pintVal = typed.intVal; break;
	case VT_UINT:   *target.puintVal = typed.uintVal; break;
	case VT_I8:     *target.pllVal = typed.llVal; break;
	case VT_UI8:    *target.pullVal = typed.ullVal; break;
	case VT_R4:     *target.pfltVal = typed.fltVal; break;
	case VT_R8:     *target.pdblVal = typed.dblVal; break;
	case VT_DATE:   *target.pdate = typed.date; break;
	case VT_CY:     *target.pcyVal = typed.cyVal; break;
	case VT_DECIMAL: *target.pdecVal = typed.decVal; break;
	default: break;
	}
	::VariantClear(&typed);
}

//*********************************************************************************************************************
//*                                                      Events                                                       *
//*********************************************************************************************************************

// ⭐⭐ THE OBJECT'S EVENTS, as calls into a sink the platform hands it. The sink is an IDispatch advised on
// the object's DEFAULT SOURCE interface (IProvideClassInfo, or the coclass in the object's type library);
// an event arrives as Invoke(DISPID), is named through the source interface's type information, and runs
// the handler the script gave for that name.
//
// ⭐ ON THE THREAD THAT OWNS THE OBJECT. In a single-threaded apartment COM guarantees it: an event comes
// while the thread is inside a call to the object (an in-process server raising it synchronously) or
// pumping messages. A thread in the multithreaded apartment gets them on COM's own threads instead — those
// are QUEUED and handed to the handler at the next call into the object, never run beside the script.
//
// 🛑 A HANDLER'S REFUSAL DOES NOT CROSS COM. An exception thrown through Invoke would unwind through the
// server's own frames; it is kept and raised when the call that let the event in returns to the script.
class ibComEventSink : public IDispatch {
public:
	ibComEventSink(const IID& iid, ITypeInfo* typeInfo, const ibString& objectName)
		: m_iid(iid), m_typeInfo(typeInfo), m_objectName(objectName), m_ownerThread(::GetCurrentThreadId()) {
		m_typeInfo->AddRef();
	}

	bool Advise(IDispatch* source) {
		IConnectionPointContainer* container = nullptr;
		if (FAILED(source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container))) || container == nullptr)
			return false;
		const HRESULT hrFind = container->FindConnectionPoint(m_iid, &m_point);
		container->Release();
		if (FAILED(hrFind) || m_point == nullptr)
			return false;
		if (FAILED(m_point->Advise(this, &m_cookie))) {
			m_point->Release();
			m_point = nullptr;
			return false;
		}
		return true;
	}

	void Unadvise() {
		if (m_point == nullptr)
			return;
		m_point->Unadvise(m_cookie);
		m_point->Release();
		m_point = nullptr;
	}

	void SetHandler(const ibString& eventName, const ibValue& handler) { m_handlers[eventName.Lower()] = handler; }
	void RemoveHandler(const ibString& eventName) { m_handlers.erase(eventName.Lower()); }
	bool HasHandlers() const { return !m_handlers.empty(); }

	bool HasEvent(const ibString& eventName) const {
		LPOLESTR name = const_cast<LPOLESTR>(eventName.wc_str());
		MEMBERID memid = MEMBERID_NIL;
		return SUCCEEDED(m_typeInfo->GetIDsOfNames(&name, 1, &memid));
	}

	// Raised on the owner's thread, after the call that let the event in has returned.
	void RaisePendingError() {
		if (!m_pendingError)
			return;
		std::exception_ptr error;
		std::swap(error, m_pendingError);
		std::rethrow_exception(error);
	}

	void DeliverQueued() {
		std::vector<ibQueuedEvent> queued;
		{
			std::lock_guard<std::mutex> lock(m_queueLock);
			queued.swap(m_queue);
		}
		for (ibQueuedEvent& event : queued) {
			DISPPARAMS params = { event.m_args.empty() ? nullptr : event.m_args.data(), nullptr, static_cast<UINT>(event.m_args.size()), 0 };
			Fire(event.m_dispid, params);
			for (VARIANT& arg : event.m_args)
				::VariantClear(&arg);
		}
		RaisePendingError();
	}

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
		if (object == nullptr)
			return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IDispatch || riid == m_iid) {
			*object = static_cast<IDispatch*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return ::InterlockedIncrement(&m_refCount); }
	STDMETHODIMP_(ULONG) Release() override {
		const ULONG count = ::InterlockedDecrement(&m_refCount);
		if (count == 0)
			delete this;
		return count;
	}

	// IDispatch
	STDMETHODIMP GetTypeInfoCount(UINT* count) override { if (count != nullptr) *count = 0; return S_OK; }
	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR* names, UINT count, LCID, DISPID* dispids) override {
		return m_typeInfo->GetIDsOfNames(names, count, dispids);
	}
	STDMETHODIMP Invoke(DISPID dispid, REFIID, LCID, WORD, DISPPARAMS* params, VARIANT* result, EXCEPINFO*, UINT*) override {
		if (result != nullptr)
			::VariantInit(result);
		if (params == nullptr)
			return E_POINTER;
		if (::GetCurrentThreadId() != m_ownerThread) {
			// Another thread: what the event carries is copied out (a by-reference argument cannot be
			// written back from here) and handed over at the owner's next call into the object.
			ibQueuedEvent event;
			event.m_dispid = dispid;
			event.m_args.resize(params->cArgs);
			for (UINT i = 0; i < params->cArgs; i++) {
				::VariantInit(&event.m_args[i]);
				::VariantCopyInd(&event.m_args[i], &params->rgvarg[i]);
			}
			std::lock_guard<std::mutex> lock(m_queueLock);
			m_queue.emplace_back(std::move(event));
			return S_OK;
		}
		Fire(dispid, *params);
		return S_OK;
	}

private:
	struct ibQueuedEvent {
		DISPID m_dispid = DISPID_UNKNOWN;
		std::vector<VARIANT> m_args;
	};

	~ibComEventSink() {
		Unadvise();
		m_typeInfo->Release();
		for (ibQueuedEvent& event : m_queue)
			for (VARIANT& arg : event.m_args)
				::VariantClear(&arg);
	}

	ibString EventName(DISPID dispid) const {
		BSTR name = nullptr;
		UINT count = 0;
		if (FAILED(m_typeInfo->GetNames(dispid, &name, 1, &count)) || count == 0)
			return ibString();
		const ibString result = FromBSTR(name);
		::SysFreeString(name);
		return result;
	}

	void Fire(DISPID dispid, DISPPARAMS& params) {
		const auto handler = m_handlers.find(EventName(dispid).Lower());
		if (handler == m_handlers.end())
			return;
		try {
			// rgvarg runs right to left: the event's first argument is the last one.
			const UINT count = params.cArgs;
			std::vector<ibValue> args(count);
			std::vector<ibValue*> argPtrs(count);
			for (UINT i = 0; i < count; i++) {
				ibValueOLE::FromVariant(params.rgvarg[count - 1 - i], args[i], m_objectName);
				argPtrs[i] = &args[i];
			}
			ibValue callable = handler->second, ret;
			InvokeLambda(callable, argPtrs.data(), static_cast<long>(count), ret);
			for (UINT i = 0; i < count; i++) {
				VARIANT& arg = params.rgvarg[count - 1 - i];
				if ((arg.vt & VT_BYREF) != 0)
					WriteByRef(arg, args[i]);
			}
		}
		catch (...) {
			if (!m_pendingError)
				m_pendingError = std::current_exception();
		}
	}

	LONG m_refCount = 1;
	const IID m_iid;
	ITypeInfo* m_typeInfo = nullptr;
	const ibString m_objectName;
	const DWORD m_ownerThread;
	IConnectionPoint* m_point = nullptr;
	DWORD m_cookie = 0;
	std::map<ibString, ibValue> m_handlers;     // by the event's name, lower-cased
	std::exception_ptr m_pendingError;
	std::mutex m_queueLock;
	std::vector<ibQueuedEvent> m_queue;
};

// The object's default source interface: the coclass says which of the interfaces it implements is the
// one it CALLS rather than answers. From the object itself (IProvideClassInfo), or from its type library
// by the class it was created as.
static ITypeInfo* FindSourceInterface(IDispatch* dispatch, const CLSID& clsId, IID& iid)
{
	ITypeInfo* classInfo = nullptr;
	IProvideClassInfo* provide = nullptr;
	if (SUCCEEDED(dispatch->QueryInterface(IID_IProvideClassInfo, reinterpret_cast<void**>(&provide))) && provide != nullptr) {
		provide->GetClassInfo(&classInfo);
		provide->Release();
	}
	if (classInfo == nullptr && clsId != CLSID_NULL) {
		ITypeInfo* typeInfo = nullptr;
		if (SUCCEEDED(dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)) && typeInfo != nullptr) {
			ITypeLib* typeLib = nullptr;
			UINT index = 0;
			if (SUCCEEDED(typeInfo->GetContainingTypeLib(&typeLib, &index)) && typeLib != nullptr) {
				typeLib->GetTypeInfoOfGuid(clsId, &classInfo);
				typeLib->Release();
			}
			typeInfo->Release();
		}
	}
	if (classInfo == nullptr)
		return nullptr;

	ITypeInfo* sourceInfo = nullptr;
	TYPEATTR* classAttr = nullptr;
	if (SUCCEEDED(classInfo->GetTypeAttr(&classAttr)) && classAttr != nullptr) {
		for (UINT i = 0; i < classAttr->cImplTypes && sourceInfo == nullptr; i++) {
			INT flags = 0;
			if (FAILED(classInfo->GetImplTypeFlags(i, &flags)))
				continue;
			if ((flags & IMPLTYPEFLAG_FDEFAULT) == 0 || (flags & IMPLTYPEFLAG_FSOURCE) == 0)
				continue;
			HREFTYPE href = 0;
			if (FAILED(classInfo->GetRefTypeOfImplType(i, &href)))
				continue;
			classInfo->GetRefTypeInfo(href, &sourceInfo);
		}
		classInfo->ReleaseTypeAttr(classAttr);
	}
	classInfo->Release();
	if (sourceInfo == nullptr)
		return nullptr;

	TYPEATTR* sourceAttr = nullptr;
	if (FAILED(sourceInfo->GetTypeAttr(&sourceAttr)) || sourceAttr == nullptr) {
		sourceInfo->Release();
		return nullptr;
	}
	iid = sourceAttr->guid;
	sourceInfo->ReleaseTypeAttr(sourceAttr);
	return sourceInfo;
}

void ibValueOLE::AddEventHandler(const ibString& eventName, const ibValue& handler)
{
	if (AsFunction(handler) == nullptr)
		ibBackendCoreException::Error(_("The handler of event \"%s\" must be a function"), eventName);

	if (m_eventSink == nullptr) {
		IID iid = IID_NULL;
		ITypeInfo* sourceInfo = FindSourceInterface(m_currentDispatch, m_clsId, iid);
		if (sourceInfo == nullptr)
			ibBackendCoreException::Error(_("COM object \"%s\" has no events"), m_objectName);
		ibComEventSink* const sink = new ibComEventSink(iid, sourceInfo, m_objectName);
		sourceInfo->Release();
		if (!sink->Advise(m_currentDispatch)) {
			sink->Release();
			ibBackendCoreException::Error(_("COM object \"%s\" has no events"), m_objectName);
		}
		m_eventSink = sink;
	}

	if (!m_eventSink->HasEvent(eventName))
		ibBackendCoreException::Error(_("COM object \"%s\" has no event \"%s\""), m_objectName, eventName);
	m_eventSink->SetHandler(eventName, handler);
}

void ibValueOLE::RemoveEventHandler(const ibString& eventName)
{
	if (m_eventSink == nullptr)
		return;
	m_eventSink->RemoveHandler(eventName);
	if (m_eventSink->HasHandlers())
		return;
	m_eventSink->Unadvise();
	m_eventSink->Release();
	m_eventSink = nullptr;
}

void ibValueOLE::DeliverQueuedEvents()
{
	if (m_eventSink != nullptr)
		m_eventSink->DeliverQueued();
}

#endif

//*********************************************************************************************************************
//*                                                      OLE Value                                                    *
//*********************************************************************************************************************

void ibValueOLE::GetInterfaceAndReleaseStream()
{
#ifdef __WXMSW__
	for (auto dispOle : gs_valueOLE) {
		ibValueOLE* oleValue = dispOle.second;
		if (!oleValue->m_streamDispatch)
			continue;
		HRESULT hr = ::CoGetInterfaceAndReleaseStream(oleValue->m_streamDispatch, IID_IDispatch, (void**)&oleValue->m_currentDispatch);
		if (SUCCEEDED(hr)) {
			oleValue->m_streamDispatch = nullptr;
		}
	}
#endif
}

#ifdef __WXMSW__
static bool createStreamForDispatch = false;
#endif

void ibValueOLE::CreateStreamForDispatch() {
#ifdef __WXMSW__
	for (auto dispOle : gs_valueOLE) {
		ibValueOLE* oleValue = dispOle.second;
		if (oleValue->m_streamDispatch)
			continue;
		HRESULT hr = ::CoMarshalInterThreadInterfaceInStream(IID_IDispatch, oleValue->m_dispatch, &oleValue->m_streamDispatch);
		if (FAILED(hr)) oleValue->m_streamDispatch = nullptr;
	}
	createStreamForDispatch = true;
#endif
}

void ibValueOLE::ReleaseStreamForDispatch()
{
#ifdef __WXMSW__
	if (createStreamForDispatch) {
		for (auto dispOle : gs_valueOLE) {
			ibValueOLE* oleValue = dispOle.second;
			oleValue->m_currentDispatch->Release();
			oleValue->m_currentDispatch = oleValue->m_dispatch;
		}
		createStreamForDispatch = false;
	}
#endif
}

void ibValueOLE::ReleaseComObjects()
{
#ifdef __WXMSW__
	for (auto dispOle : gs_valueOLE) {
		ibValueOLE* oleValue = dispOle.second;
		if (!oleValue->m_dispatch)
			continue;
		unsigned int countRef = oleValue->m_dispatch->Release();
		for (unsigned int i = 0; i < countRef; i++) {
			oleValue->m_dispatch->Release();
		}
		oleValue->m_dispatch = nullptr;
	}
	createStreamForDispatch = false;
#endif
}

#ifdef __WXMSW__

IDispatch* ibValueOLE::DoCreateInstance()
{
	// get the server IDispatch interface
	//
	// NB: using CLSCTX_INPROC_HANDLER results in failure when getting
	//     Automation interface for Microsoft Office applications so don't use
	//     CLSCTX_ALL which includes it

	IDispatch* pDispatch = nullptr;
	const HRESULT hr = ::CoCreateInstance(m_clsId, nullptr, CLSCTX_SERVER,
		IID_IDispatch, (void**)&pDispatch);

	if (FAILED(hr))
		ibBackendCoreException::Error(_("COM object \"%s\" could not be created: %s"), m_objectName, HResultText(hr));

	return pDispatch;
}

ibString ibValueOLE::MemberName(const long dispid) const
{
	const auto it = m_memberNames.find(dispid);
	return it != m_memberNames.end() ? it->second : ibString::Format(ibT("%ld"), dispid);
}

const std::vector<bool>& ibValueOLE::ByRefArgs(const long dispid) const
{
	const auto known = m_byRefArgs.find(dispid);
	if (known != m_byRefArgs.end())
		return known->second;

	std::vector<bool>& byRef = m_byRefArgs[dispid];
	ITypeInfo* typeInfo = nullptr;
	if (m_currentDispatch == nullptr || FAILED(m_currentDispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)) || typeInfo == nullptr)
		return byRef;

	TYPEATTR* typeAttr = nullptr;
	if (SUCCEEDED(typeInfo->GetTypeAttr(&typeAttr)) && typeAttr != nullptr) {
		for (UINT f = 0; f < typeAttr->cFuncs && byRef.empty(); f++) {
			FUNCDESC* funcDesc = nullptr;
			if (FAILED(typeInfo->GetFuncDesc(f, &funcDesc)) || funcDesc == nullptr)
				continue;
			if (funcDesc->memid == dispid && (funcDesc->invkind == INVOKE_FUNC || funcDesc->invkind == INVOKE_PROPERTYGET)) {
				byRef.resize(funcDesc->cParams, false);
				for (SHORT p = 0; p < funcDesc->cParams; p++) {
					const ELEMDESC& param = funcDesc->lprgelemdescParam[p];
					byRef[p] = (param.paramdesc.wParamFlags & PARAMFLAG_FOUT) != 0 || param.tdesc.vt == VT_PTR;
				}
			}
			typeInfo->ReleaseFuncDesc(funcDesc);
		}
		typeInfo->ReleaseTypeAttr(typeAttr);
	}
	typeInfo->Release();
	return byRef;
}

void ibValueOLE::Invoke(const long dispid, unsigned short flags, DISPPARAMS& params, VARIANT* result) const
{
	EXCEPINFO excepInfo = {};
	UINT argErr = 0;
	const HRESULT hr = m_currentDispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, &excepInfo, &argErr);
	if (m_eventSink != nullptr)
		m_eventSink->RaisePendingError();
	if (SUCCEEDED(hr))
		return;

	// The server's own words when it has some (DISP_E_EXCEPTION, filled in now if it deferred that);
	// otherwise what the system says the code means. Either way the object and the member are named.
	ibString description;
	if (hr == DISP_E_EXCEPTION) {
		if (excepInfo.pfnDeferredFillIn != nullptr)
			excepInfo.pfnDeferredFillIn(&excepInfo);
		description = FromBSTR(excepInfo.bstrDescription);
		if (description.IsEmpty())
			description = HResultText(excepInfo.scode != 0 ? excepInfo.scode : hr);
	}
	else if (hr == DISP_E_TYPEMISMATCH || hr == DISP_E_PARAMNOTFOUND) {
		description = ibString::Format(_("argument %d: %s"), static_cast<int>(params.cArgs - argErr), HResultText(hr));
	}
	else {
		description = HResultText(hr);
	}
	::SysFreeString(excepInfo.bstrSource);
	::SysFreeString(excepInfo.bstrDescription);
	::SysFreeString(excepInfo.bstrHelpFile);

	ibBackendCoreException::Error(_("COM object \"%s\", member \"%s\": %s"), m_objectName, MemberName(dispid), description);
}

#endif

#ifdef __WXMSW__
ibValueOLE::ibValueOLE() : ibValueDynamicMembers(ibValueTypes::TYPE_OLE),
m_clsId(CLSID_NULL), m_dispatch(nullptr), m_currentDispatch(nullptr),
m_objectName(ibEmptyString)
{
	m_members.Bind(this, &ibValueOLE::FillMembers);
}

ibValueOLE::ibValueOLE(const CLSID& clsId, IDispatch* dispatch, const ibString& objectName) : ibValueDynamicMembers(ibValueTypes::TYPE_OLE),
m_clsId(clsId), m_dispatch(dispatch), m_currentDispatch(nullptr),
m_objectName(objectName)
{
	m_members.Bind(this, &ibValueOLE::FillMembers);
	if (m_dispatch != nullptr) {
		m_dispatch->AddRef();
	}
	if (createStreamForDispatch) {
		if (m_dispatch != nullptr) {
			HRESULT marshalHr =
				::CoMarshalInterThreadInterfaceInStream(IID_IDispatch, m_dispatch, &m_streamDispatch);
			if (FAILED(marshalHr))
				m_streamDispatch = nullptr;
		}
		if (m_streamDispatch != nullptr) {
			HRESULT unmarshalHr = ::CoGetInterfaceAndReleaseStream(m_streamDispatch, IID_IDispatch, (void**)&m_currentDispatch);
			if (SUCCEEDED(unmarshalHr)) {
				m_streamDispatch = nullptr;
			}
		}
	}
	else {
		m_currentDispatch = m_dispatch;
	}
}
#else
ibValueOLE::ibValueOLE() : ibValueDynamicMembers(ibValueTypes::TYPE_OLE), m_objectName(ibEmptyString) {
	m_members.Bind(this, &ibValueOLE::FillMembers);
}
#endif

ibValueOLE::~ibValueOLE()
{
#ifdef __WXMSW__
	if (m_eventSink != nullptr) {
		m_eventSink->Unadvise();
		m_eventSink->Release();
		m_eventSink = nullptr;
	}
	if (m_dispatch != nullptr) {
		// Only the entry that is this value's: an object a member handed back may share the interface
		// pointer, and dropping it must not unregister the object it came from.
		const auto registered = gs_valueOLE.find(m_dispatch);
		if (registered != gs_valueOLE.end() && registered->second == this)
			gs_valueOLE.erase(registered);
		m_dispatch->Release();
		m_dispatch = nullptr;
	}
#endif
}

bool ibValueOLE::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	const ibString& strOleName = paParams[0]->GetString();
	if (!Create(strOleName))
		ibBackendCoreException::Error(_("No COM class is registered as \"%s\""), strOleName);
	return true;
}

bool ibValueOLE::Create(const ibString& strOleName)
{
	// THE CARET'S WALK FOR COMPLETION STARTS NO COM SERVER: it builds `New COMObject("…")` to list its members,
	// and that must not launch the application named. Asked of the evaluation, not of the process — the
	// designer runs no other script, and the client's editors complete with the same walk (census, 2026-10-01).
	if (ibBackendException::IsEvalComplete())
		return true;

#ifdef __WXMSW__
	if (m_dispatch != nullptr)
		return false;

	EnterComApartment();

	HRESULT hr = ::CLSIDFromProgID(strOleName.wc_str(), &m_clsId);
	if (FAILED(hr))
		return false;

	m_objectName = strOleName;
	m_dispatch = DoCreateInstance();
	InvalidateNames();   // COM surface depends on the freshly created dispatch
	if (createStreamForDispatch) {
		if (m_dispatch != nullptr) {
			HRESULT marshalHr =
				::CoMarshalInterThreadInterfaceInStream(IID_IDispatch, m_dispatch, &m_streamDispatch);
			if (FAILED(marshalHr)) {
				m_streamDispatch = nullptr;
			}
		}
		if (m_streamDispatch != nullptr) {
			HRESULT unmarshalHr = ::CoGetInterfaceAndReleaseStream(m_streamDispatch, IID_IDispatch, (void**)&m_currentDispatch);
			if (SUCCEEDED(unmarshalHr)) {
				m_streamDispatch = nullptr;
			}
		}
		if (m_currentDispatch == nullptr)
			return false;
	}
	else {
		m_currentDispatch = m_dispatch;
	}
	gs_valueOLE.insert_or_assign(m_dispatch, this);
	return m_dispatch != nullptr;
#else
	// The type is here so that a module which creates one compiles on every platform; the object is not.
	ibBackendCoreException::Error(_("COM objects are available on Windows only"));
#endif
}

void ibValueOLE::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProc(ibT("AddEventHandler"), 2, ibT("AddEventHandler(eventName : string, handler : function)"), s_methodAddEventHandler);
	helper.AppendProc(ibT("RemoveEventHandler"), 1, ibT("RemoveEventHandler(eventName : string)"), s_methodRemoveEventHandler);

#ifdef __WXMSW__
	if (m_dispatch == nullptr)
		return;
	unsigned int count = 0;
	if (m_dispatch->GetTypeInfoCount(&count) != S_OK)
		return;
	for (unsigned int i = 0; i < count; i++) {

		HRESULT hr = S_OK;
		ITypeInfo* typeInfo = nullptr; TYPEATTR* typeAttr = nullptr;

		hr = m_dispatch->GetTypeInfo(i, LOCALE_SYSTEM_DEFAULT, &typeInfo);
		if (hr != S_OK)
			continue;

		hr = typeInfo->GetTypeAttr(&typeAttr);
		if (hr != S_OK) {
			typeInfo->Release();
			continue;
		}

		if (typeAttr->typekind == TKIND_INTERFACE ||
			typeAttr->typekind == TKIND_DISPATCH) {
			for (unsigned int m = 0; m < (UINT)typeAttr->cFuncs; m++)
			{
				FUNCDESC* funcInfo = nullptr;
				hr = typeInfo->GetFuncDesc(m, &funcInfo);

				if (hr != S_OK) continue;
				if (funcInfo->invkind != INVOKE_FUNC && funcInfo->invkind != INVOKE_PROPERTYGET) {
					typeInfo->ReleaseFuncDesc(funcInfo);
					continue;
				}

				BSTR strName = nullptr;
				// read the method name
				typeInfo->GetDocumentation(funcInfo->memid, &strName,
					nullptr, nullptr, nullptr);

				const ibString strMethodName = FromBSTR(strName);
				::SysFreeString(strName);

				ibString methodHelper = strMethodName + ibT("(");

				for (unsigned int k = 0; k < (UINT)funcInfo->cParams; k++) {
					VARTYPE vt = funcInfo->lprgelemdescParam[k].tdesc.vt & ~0xF000;
					if (vt <= VT_CLSID)
					{
						switch (vt)
						{
						case VT_BOOL:
							methodHelper += ibT("boolean");
							break;
						case VT_UI1:
						case VT_I2:
						case VT_I4:
						case VT_I8:
						case VT_R4:
						case VT_R8:
						case VT_I1:
						case VT_UI2:
						case VT_UI4:
						case VT_INT:
						case VT_UINT:
						case VT_CY:
						case VT_DECIMAL:
							methodHelper += ibT("number");
							break;
						case VT_BSTR:
							methodHelper += ibT("string");
							break;
						case VT_DATE:
							methodHelper += ibT("date");
							break;
						case VT_DISPATCH:
							methodHelper += ibT("oleRef");
							break;
						case VT_PTR:
							methodHelper += ibT("olePtr");
							break;
						case VT_SAFEARRAY:
							methodHelper += ibT("oleArray");
							break;
						case VT_NULL:
							methodHelper += ibT("null");
							break;
						case VT_EMPTY:
							methodHelper += ibT("empty");
							break;
						default: methodHelper += ibT("other"); break;
						}
					}
					else methodHelper += ibT("badType");
					if (k < (UINT)funcInfo->cParams - 1) {
						methodHelper += ibT(", ");
					}
				}
				methodHelper += ibT(")");
				if (funcInfo->invkind == INVOKE_FUNC) {
					helper.AppendFunc(
						strMethodName,
						funcInfo->cParams,
						methodHelper,
						funcInfo->memid
					);
				}
				else if (funcInfo->invkind == INVOKE_PROPERTYGET) {
					helper.AppendProp(
						strMethodName,
						funcInfo->memid
					);
				}
				typeInfo->ReleaseFuncDesc(funcInfo);
			}
		}
		typeInfo->ReleaseTypeAttr(typeAttr);
		typeInfo->Release();
	}
#endif
}

long ibValueOLE::FindMethod(const ibString& strMethodName) const
{
	if (strMethodName.IsSameAs(ibT("AddEventHandler"), false))
		return s_methodAddEventHandler;
	if (strMethodName.IsSameAs(ibT("RemoveEventHandler"), false))
		return s_methodRemoveEventHandler;
	return FindProp(strMethodName);
}

long ibValueOLE::FindProp(const ibString& strPropName) const
{
#ifdef __WXMSW__
	if (m_currentDispatch == nullptr)
		return wxNOT_FOUND;
	LPOLESTR name = const_cast<LPOLESTR>(strPropName.wc_str());
	DISPID dispid = DISPID_UNKNOWN;
	if (FAILED(m_currentDispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid)))
		return wxNOT_FOUND;
	m_memberNames[dispid] = strPropName;
	return dispid;
#else
	return wxNOT_FOUND;
#endif
}

long ibValueOLE::GetNParams(const long lMethodNum) const
{
	if (lMethodNum == s_methodAddEventHandler)
		return 2;
	if (lMethodNum == s_methodRemoveEventHandler)
		return 1;
	return s_comArgsBound;
}

bool ibValueOLE::HasRetVal(const long lMethodNum) const
{
	return lMethodNum != s_methodAddEventHandler && lMethodNum != s_methodRemoveEventHandler;
}

bool ibValueOLE::GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const
{
#ifdef __WXMSW__
	// A HOLE IN THE CALL (`f(a, , c)`) — the interpreter asks for its default and this is the only moment
	// the object hears of it. Noted, and sent as the automation "missing" rather than as an Undefined.
	m_omittedArgs.push_back(lParamNum);
#endif
	return false;
}

#ifdef __WXMSW__

static HRESULT PutProperty(
	IDispatch* pDisp,
	DISPID dwDispID,
	VARIANT* pVar,
	EXCEPINFO* excepInfo)
{
	DISPPARAMS dispparams = { nullptr, nullptr, 1, 1 };
	dispparams.rgvarg = pVar;
	DISPID dispidPut = DISPID_PROPERTYPUT;
	dispparams.rgdispidNamedArgs = &dispidPut;

	if (pVar->vt == VT_UNKNOWN
		|| pVar->vt == VT_DISPATCH
		|| (pVar->vt & VT_ARRAY)
		|| (pVar->vt & VT_BYREF)) {
		HRESULT hr = pDisp->Invoke(
			dwDispID,
			IID_NULL,
			LOCALE_USER_DEFAULT,
			DISPATCH_PROPERTYPUTREF,
			&dispparams,
			nullptr,
			excepInfo,
			nullptr
		);
		if (SUCCEEDED(hr))
			return hr;
	}

	return pDisp->Invoke(
		dwDispID,
		IID_NULL,
		LOCALE_USER_DEFAULT,
		DISPATCH_PROPERTYPUT,
		&dispparams,
		nullptr,
		excepInfo,
		nullptr
	);
};

#endif

bool ibValueOLE::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
#ifdef __WXMSW__
	if (m_currentDispatch == nullptr)
		return false;
	DeliverQueuedEvents();

	VARIANT oleVariant;
	ToVariant(varPropVal, oleVariant);
	EXCEPINFO excepInfo = {};
	const HRESULT hr = PutProperty(m_currentDispatch, lPropNum, &oleVariant, &excepInfo);
	::VariantClear(&oleVariant);
	if (m_eventSink != nullptr)
		m_eventSink->RaisePendingError();
	if (FAILED(hr)) {
		if (hr == DISP_E_EXCEPTION && excepInfo.pfnDeferredFillIn != nullptr)
			excepInfo.pfnDeferredFillIn(&excepInfo);
		ibString description = FromBSTR(excepInfo.bstrDescription);
		if (description.IsEmpty())
			description = HResultText(hr);
		::SysFreeString(excepInfo.bstrSource);
		::SysFreeString(excepInfo.bstrDescription);
		::SysFreeString(excepInfo.bstrHelpFile);
		ibBackendCoreException::Error(_("COM object \"%s\", member \"%s\": %s"), m_objectName, MemberName(lPropNum), description);
	}
	return true;
#else
	return false;
#endif
}

bool ibValueOLE::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
#ifdef __WXMSW__
	if (m_currentDispatch == nullptr)
		return false;
	DeliverQueuedEvents();

	VARIANT result;
	::VariantInit(&result);
	DISPPARAMS params = { nullptr, nullptr, 0, 0 };
	Invoke(lPropNum, DISPATCH_PROPERTYGET, params, &result);
	try {
		FromVariant(result, pvarPropVal, m_objectName);
	}
	catch (...) {
		::VariantClear(&result);
		throw;
	}
	::VariantClear(&result);
	return true;
#else
	return false;
#endif
}

bool ibValueOLE::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
#ifdef __WXMSW__
	std::vector<long> omitted;
	omitted.swap(m_omittedArgs);

	if (lMethodNum == s_methodAddEventHandler) {
		if (m_currentDispatch == nullptr)
			return false;
		AddEventHandler(paParams[0]->GetString(), *paParams[1]);
		return true;
	}
	if (lMethodNum == s_methodRemoveEventHandler) {
		RemoveEventHandler(paParams[0]->GetString());
		return true;
	}

	if (m_currentDispatch == nullptr)
		return false;
	DeliverQueuedEvents();

	// rgvarg runs right to left: the first argument is the last VARIANT. An argument the member declares
	// [out] goes as a reference to a VARIANT of its own, and what the object wrote there comes back to the
	// caller's variable; the rest go by value.
	const std::vector<bool>& byRefDeclared = ByRefArgs(lMethodNum);
	std::vector<VARIANT> args(lSizeArray), byRefValues(lSizeArray);
	std::vector<bool> byRef(lSizeArray, false);
	for (long arg = 0; arg < lSizeArray; arg++) {
		::VariantInit(&args[arg]);
		::VariantInit(&byRefValues[arg]);
	}

	auto clearArgs = [&]() {
		for (long arg = 0; arg < lSizeArray; arg++) {
			if (!byRef[arg])
				::VariantClear(&args[arg]);
			::VariantClear(&byRefValues[arg]);
		}
	};

	VARIANT result;
	::VariantInit(&result);
	try {
		for (long arg = 0; arg < lSizeArray; arg++) {
			VARIANT& slot = args[lSizeArray - arg - 1];
			if (std::find(omitted.begin(), omitted.end(), arg) != omitted.end()) {
				slot.vt = VT_ERROR;
				slot.scode = DISP_E_PARAMNOTFOUND;
				continue;
			}
			if (arg < static_cast<long>(byRefDeclared.size()) && byRefDeclared[arg]) {
				ToVariant(*paParams[arg], byRefValues[arg]);
				slot.vt = VT_BYREF | VT_VARIANT;
				slot.pvarVal = &byRefValues[arg];
				byRef[lSizeArray - arg - 1] = true;
				continue;
			}
			ToVariant(*paParams[arg], slot);
		}

		DISPPARAMS params = { lSizeArray > 0 ? args.data() : nullptr, nullptr, static_cast<UINT>(lSizeArray), 0 };
		Invoke(lMethodNum, DISPATCH_METHOD | DISPATCH_PROPERTYGET, params, &result);

		for (long arg = 0; arg < lSizeArray; arg++) {
			if (byRef[lSizeArray - arg - 1])
				FromVariant(byRefValues[arg], *paParams[arg], m_objectName);
		}
		FromVariant(result, pvarRetValue, m_objectName);
	}
	catch (...) {
		clearArgs();
		::VariantClear(&result);
		throw;
	}
	clearArgs();
	::VariantClear(&result);
	return true;
#else
	return false;
#endif
}

#ifdef __WXMSW__

// `For Each` over the object: its _NewEnum, walked one VARIANT at a time.
class ibComEnumState : public ibValueIteratorState {
public:
	ibComEnumState(IEnumVARIANT* enumerator, const ibString& objectName)
		: m_enumerator(enumerator), m_objectName(objectName) {
	}
	~ibComEnumState() override {
		m_enumerator->Release();
	}
	bool MoveNext(ibValue& current) override {
		VARIANT item;
		::VariantInit(&item);
		ULONG fetched = 0;
		if (m_enumerator->Next(1, &item, &fetched) != S_OK || fetched == 0)
			return false;
		try {
			ibValueOLE::FromVariant(item, current, m_objectName);
		}
		catch (...) {
			::VariantClear(&item);
			throw;
		}
		::VariantClear(&item);
		return true;
	}
	void Reset() override {
		m_enumerator->Reset();
	}
private:
	IEnumVARIANT* m_enumerator;
	const ibString m_objectName;
};

#endif

std::shared_ptr<ibValueIteratorState> ibValueOLE::CreateIterator()
{
#ifdef __WXMSW__
	if (m_currentDispatch == nullptr)
		return nullptr;

	VARIANT result;
	::VariantInit(&result);
	DISPPARAMS params = { nullptr, nullptr, 0, 0 };
	EXCEPINFO excepInfo = {};
	const HRESULT hr = m_currentDispatch->Invoke(DISPID_NEWENUM, IID_NULL, LOCALE_USER_DEFAULT,
		DISPATCH_METHOD | DISPATCH_PROPERTYGET, &params, &result, &excepInfo, nullptr);
	::SysFreeString(excepInfo.bstrSource);
	::SysFreeString(excepInfo.bstrDescription);
	::SysFreeString(excepInfo.bstrHelpFile);

	IEnumVARIANT* enumerator = nullptr;
	if (SUCCEEDED(hr) && (result.vt == VT_UNKNOWN || result.vt == VT_DISPATCH) && result.punkVal != nullptr)
		result.punkVal->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(&enumerator));
	::VariantClear(&result);

	if (enumerator == nullptr)
		ibBackendCoreException::Error(_("COM object \"%s\" cannot be walked with For Each"), m_objectName);
	return std::make_shared<ibComEnumState>(enumerator, m_objectName);
#else
	return nullptr;
#endif
}

#pragma warning(pop)

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueOLE, "ComObject", value_to_clsid("VL_OLE"));
