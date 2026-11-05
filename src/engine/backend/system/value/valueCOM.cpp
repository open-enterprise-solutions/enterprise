////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, Vadim Zeitlin
//	Description : OLE-supporter
////////////////////////////////////////////////////////////////////////////

#include "valueCOM.h"
#include "valueArray.h"
#include "valueBinaryData.h"
#include "valueEvent.h"                         // ibValueEvent — a handler by name, the platform's named dispatcher
#include "valueSafeArray.h"                     // ibValueSafeArray — an array, both ways
#include "backend/backend_exception.h"
#include "backend/compiler/procUnitLambda.h"    // AsFunction — a handler as a lambda; the session, GetPUState with it

#ifdef __IBMSW__
#include <ocidl.h>      // IConnectionPointContainer, IProvideClassInfo

#include <algorithm>
#include <cstdint>
#include <cstring>
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

#ifdef __IBMSW__
using Microsoft::WRL::ComPtr;

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

//*********************************************************************************************************************
//*                                                      Conversion                                                   *
//*********************************************************************************************************************

static ibString InterfaceName(IDispatch* dispatch, const ibString& fallback)
{
	ComPtr<ITypeInfo> typeInfo;
	if (FAILED(dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)) || typeInfo == nullptr)
		return fallback;
	BSTR name = nullptr;
	ibString result = fallback;
	if (SUCCEEDED(typeInfo->GetDocumentation(MEMBERID_NIL, &name, nullptr, nullptr, nullptr)) && name != nullptr)
		result = FromBSTR(name);
	::SysFreeString(name);
	return result;
}

void ibValueCOM::FromVariant(const VARIANT& oleVariant, ibValue& value, const ibString& objectName)
{
	// BY REFERENCE: read what it points at — VariantCopyInd takes every kind of reference apart.
	if ((oleVariant.vt & VT_BYREF) != 0) {
		VARIANT direct;
		::VariantInit(&direct);
		const HRESULT hr = ::VariantCopyInd(&direct, const_cast<VARIANT*>(&oleVariant));
		if (FAILED(hr))
			ibBackendCoreException::Error(_("COM object \"%s\" returned a value that cannot be read: %s"), objectName, ibSysErrorMsgStr(hr));
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

	// AN ARRAY IS A ComSafeArray, a copy of it as it came: nothing is read until the script asks.
	if ((oleVariant.vt & VT_ARRAY) != 0) {
		if (oleVariant.parray == nullptr) {
			value.Reset();
			return;
		}
		value = new ibValueSafeArray(oleVariant);
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
		value = ibNumber(oleVariant.cVal);
		return;
	case VT_UI1:
		value = ibNumber(oleVariant.bVal);
		return;
	case VT_I2:
		value = ibNumber(oleVariant.iVal);
		return;
	case VT_UI2:
		value = ibNumber(oleVariant.uiVal);
		return;
	case VT_I4:
		value = ibNumber(oleVariant.lVal);
		return;
	case VT_UI4:
		value = ibNumber(oleVariant.ulVal);
		return;
	case VT_INT:
		value = ibNumber(oleVariant.intVal);
		return;
	case VT_UINT:
		value = ibNumber(oleVariant.uintVal);
		return;
	case VT_I8:
		value = ibNumber(oleVariant.llVal);
		return;
	case VT_UI8:
		value = ibNumber(oleVariant.ullVal);
		return;
	case VT_R8:
		value = ibNumber(oleVariant.dblVal);   // the decimal a double stands for (fnumber.cpp)
		return;
	case VT_CY:
		// Currency is an integer of ten-thousandths: divided exactly, never through a double.
		value = ibNumber(oleVariant.cyVal.int64) / ibNumber(10000);
		return;
	case VT_R4:
	case VT_DECIMAL: {
		// Written out by OLE in the invariant locale and read back from the text: a DECIMAL's 96 bits and a scale
		// exactly, a float at its own digits. Through a double a float carried its noise: 0.1 read 0.100000001490116.
		VARIANT text;
		::VariantInit(&text);
		const HRESULT hr = ::VariantChangeTypeEx(&text, const_cast<VARIANT*>(&oleVariant), LOCALE_INVARIANT, 0, VT_BSTR);
		if (FAILED(hr))
			ibBackendCoreException::Error(_("COM object \"%s\" returned a value that cannot be read: %s"), objectName, ibSysErrorMsgStr(hr));
		const ibString digits = FromBSTR(text.bstrVal);
		::VariantClear(&text);
		value = ibNumber(digits);
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
			value = new ibValueCOM(CLSID_NULL, oleVariant.pdispVal, InterfaceName(oleVariant.pdispVal, objectName));
		return;
	case VT_UNKNOWN: {
		// An interface that answers IDispatch is an object like any other; one that does not cannot be
		// called by name, and saying so is better than handing back an Undefined that hides it.
		if (oleVariant.punkVal == nullptr) {
			value.Reset();
			return;
		}
		ComPtr<IDispatch> dispatch;
		if (FAILED(oleVariant.punkVal->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(dispatch.GetAddressOf()))) || dispatch == nullptr)
			ibBackendCoreException::Error(_("COM object \"%s\" returned an interface that cannot be called by name"), objectName);
		value = new ibValueCOM(CLSID_NULL, dispatch.Get(), InterfaceName(dispatch.Get(), objectName));
		return;
	}
	}

	ibBackendCoreException::Error(_("COM object \"%s\" returned a value of a kind the language does not have (VARTYPE %d)"),
		objectName, static_cast<int>(oleVariant.vt));
}

// The significant digits of a number's text — the leading zeros do not count.
static size_t SignificantDigits(const ibString& text)
{
	size_t count = 0;
	bool started = false;
	for (const ibChar c : text) {
		if (c < ibT('0') || c > ibT('9'))
			continue;
		started = started || c != ibT('0');
		if (started)
			++count;
	}
	return count;
}

void ibValueCOM::FromValue(const ibValue& value, VARIANT& oleVariant)
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
		// a fraction as a double — an old server refuses VT_I8 outright, and every one reads VT_R8 — unless
		// it has more digits than a double keeps (15): then as a DECIMAL, which keeps 28. A double took
		// 12345678901234.123456 into a DECIMAL field as 12345678901234.1.
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
		const ibString text = number.ToString();
		if (SignificantDigits(text) > 15 && SUCCEEDED(::VarDecFromStr(text.wc_str(), LOCALE_INVARIANT, 0, &oleVariant.decVal))) {
			oleVariant.vt = VT_DECIMAL;      // after the value: a DECIMAL's first word is where vt lives
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

	ibValueCOM* valueCOM = nullptr;
	if (value.ConvertToValue(valueCOM) && valueCOM != nullptr && valueCOM->m_dispatch != nullptr) {
		oleVariant.vt = VT_DISPATCH;
		valueCOM->m_dispatch.CopyTo(&oleVariant.pdispVal);
		return;
	}

	ibValueBinaryData* binaryData = nullptr;
	if (value.ConvertToValue(binaryData) && binaryData != nullptr) {
		// Bytes go as automation's own array of bytes, the way a file or a picture is passed.
		const ibMemoryBuffer& bytes = binaryData->GetBuffer();
		SAFEARRAY* psa = ::SafeArrayCreateVector(VT_UI1, 0, static_cast<ULONG>(bytes.GetDataLen()));
		if (psa == nullptr)
			ibBackendCoreException::Error(_("An array of %d elements cannot be passed to a COM object"), static_cast<int>(bytes.GetDataLen()));
		if (!bytes.IsEmpty())
			std::memcpy(psa->pvData, bytes.GetData(), bytes.GetDataLen());
		oleVariant.vt = VT_ARRAY | VT_UI1;
		oleVariant.parray = psa;
		return;
	}

	ibValueSafeArray* safeArray = nullptr;
	if (value.ConvertToValue(safeArray) && safeArray != nullptr) {
		safeArray->CopyTo(oleVariant);
		return;
	}

	// An Array as VARIANTs, rows of one length as one table (ibValueSafeArray::FromValueArray).
	ibValueArray* valueArray = nullptr;
	if (value.ConvertToValue(valueArray) && valueArray != nullptr) {
		oleVariant = ibValueSafeArray::FromValueArray(valueArray, VT_VARIANT, {});
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
		ibValueCOM::FromValue(value, *target.pvarVal);
		return;
	}

	VARIANT source, typed;
	ibValueCOM::FromValue(value, source);
	::VariantInit(&typed);
	const HRESULT hr = ::VariantChangeType(&typed, &source, 0, vt);
	::VariantClear(&source);
	if (FAILED(hr))
		return;     // the kind the caller declared cannot hold it: the argument keeps what it had

	if ((vt & VT_ARRAY) != 0) {
		::SafeArrayDestroy(*target.pparray);
		*target.pparray = typed.parray;         // the array moves over, not copied
		return;
	}

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
// a handler is kept by the DISPID its event's name has in the source interface (EventId), and an event that
// arrives as Invoke(DISPID) runs the one kept for it.
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
	}

	bool Advise(IDispatch* source) {
		ComPtr<IConnectionPointContainer> container;
		if (FAILED(source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(container.GetAddressOf()))) || container == nullptr)
			return false;
		if (FAILED(container->FindConnectionPoint(m_iid, &m_point)) || m_point == nullptr || FAILED(m_point->Advise(this, &m_cookie))) {
			m_point.Reset();
			return false;
		}
		return true;
	}

	void Unadvise() {
		if (m_point == nullptr)
			return;
		m_point->Unadvise(m_cookie);
		m_point.Reset();
	}

	void SetHandler(const DISPID dispid, const ibValue& dispatcher, const std::weak_ptr<ibProcUnit>& runtime) {
		m_handlers[dispid] = { dispatcher, runtime };
	}
	void RemoveHandler(const DISPID dispid) { m_handlers.erase(dispid); }
	bool HasHandlers() const { return !m_handlers.empty(); }

	// The event's DISPID by its name, in any case; DISPID_UNKNOWN when the object has no such event. Read off the
	// source interface's functions one by one, not asked of ITypeInfo::GetIDsOfNames: that one is [local] in oaidl.idl
	// and fails on the proxy an out-of-process server's type information is (2026-11-03: no Excel event was found).
	DISPID EventId(const ibString& eventName) const {
		TYPEATTR* attr = nullptr;
		if (FAILED(m_typeInfo->GetTypeAttr(&attr)) || attr == nullptr)
			return DISPID_UNKNOWN;
		DISPID found = DISPID_UNKNOWN;
		for (UINT i = 0; i < attr->cFuncs && found == DISPID_UNKNOWN; i++) {
			FUNCDESC* func = nullptr;
			if (FAILED(m_typeInfo->GetFuncDesc(i, &func)) || func == nullptr)
				continue;
			if (EventName(func->memid).IsSameAs(eventName, false))
				found = func->memid;
			m_typeInfo->ReleaseFuncDesc(func);
		}
		m_typeInfo->ReleaseTypeAttr(attr);
		return found;
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
		if (names == nullptr || dispids == nullptr || count != 1)
			return DISP_E_UNKNOWNNAME;
		dispids[0] = EventId(ibString(names[0]));
		return dispids[0] != DISPID_UNKNOWN ? S_OK : DISP_E_UNKNOWNNAME;
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

	// A handler: the value that dispatches the event (ibEventDispatcher — a lambda, or a named event) and the
	// module a named one is called through, held weakly (procUnit.h): a form that closed takes its module with it.
	// A lambda finds its own runtime, and its m_runtime stays empty.
	struct ibHandler {
		ibValue m_dispatcher;
		std::weak_ptr<ibProcUnit> m_runtime;
	};

	~ibComEventSink() {
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
		const auto handler = m_handlers.find(dispid);
		if (handler == m_handlers.end())
			return;
		try {
			// rgvarg runs right to left: the event's first argument is the last one.
			const UINT count = params.cArgs;
			std::vector<ibValue> args(count);
			std::vector<ibValue*> argPtrs(count);
			for (UINT i = 0; i < count; i++) {
				ibValueCOM::FromVariant(params.rgvarg[count - 1 - i], args[i], m_objectName);
				argPtrs[i] = &args[i];
			}
			// Through the platform's own road for an event (ibEventDispatcher), the one a form's event takes: a lambda
			// is its own dispatcher, a named event calls its procedure through the module that subscribed it, and
			// either takes as many of the event's arguments as it declares.
			ibEventDispatcher* dispatcher = AsFunction(handler->second.m_dispatcher);
			ibValueEvent* named = nullptr;
			if (dispatcher == nullptr && handler->second.m_dispatcher.ConvertToValue(named))
				dispatcher = named;
			// A named handler whose module is gone — the object outlived the form that subscribed — is refused, as a
			// handler's own error is, from the call the event came in.
			const std::shared_ptr<ibProcUnit> runtime = handler->second.m_runtime.lock();
			if (named != nullptr && runtime == nullptr)
				ibBackendCoreException::Error(_("The module that handles event \"%s\" of COM object \"%s\" is gone"),
					EventName(dispid), m_objectName);
			ibValue cancel;
			dispatcher->Dispatch(runtime.get(), argPtrs.data(), static_cast<long>(count), cancel);
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
	const ComPtr<ITypeInfo> m_typeInfo;
	const ibString m_objectName;
	const DWORD m_ownerThread;
	ComPtr<IConnectionPoint> m_point;
	DWORD m_cookie = 0;
	std::map<DISPID, ibHandler> m_handlers;       // by the event's DISPID (EventId)
	std::exception_ptr m_pendingError;
	std::mutex m_queueLock;
	std::vector<ibQueuedEvent> m_queue;
};

void ibComEventSinkRelease::operator()(ibComEventSink* sink) const
{
	sink->Unadvise();
	sink->Release();
}

// The object's default source interface: the coclass says which of the interfaces it implements is the
// one it CALLS rather than answers. From the object itself (IProvideClassInfo), or from its type library
// by the class it was created as.
static ComPtr<ITypeInfo> FindSourceInterface(IDispatch* dispatch, const CLSID& clsId, IID& iid)
{
	ComPtr<ITypeInfo> classInfo;
	ComPtr<IProvideClassInfo> provide;
	if (SUCCEEDED(dispatch->QueryInterface(IID_IProvideClassInfo, reinterpret_cast<void**>(provide.GetAddressOf()))) && provide != nullptr)
		provide->GetClassInfo(&classInfo);
	if (classInfo == nullptr && clsId != CLSID_NULL) {
		ComPtr<ITypeInfo> typeInfo;
		if (SUCCEEDED(dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)) && typeInfo != nullptr) {
			ComPtr<ITypeLib> typeLib;
			UINT index = 0;
			if (SUCCEEDED(typeInfo->GetContainingTypeLib(&typeLib, &index)) && typeLib != nullptr)
				typeLib->GetTypeInfoOfGuid(clsId, &classInfo);
		}
	}
	if (classInfo == nullptr)
		return nullptr;

	ComPtr<ITypeInfo> sourceInfo;
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
	if (sourceInfo == nullptr)
		return nullptr;

	TYPEATTR* sourceAttr = nullptr;
	if (FAILED(sourceInfo->GetTypeAttr(&sourceAttr)) || sourceAttr == nullptr)
		return nullptr;
	iid = sourceAttr->guid;
	sourceInfo->ReleaseTypeAttr(sourceAttr);
	return sourceInfo;
}

void ibValueCOM::AddEventHandler(const ibString& eventName, const ibValue& handler)
{
	// ⭐ A LAMBDA, OR A PROCEDURE OF THE MODULE THAT SUBSCRIBES — by its name, or as the named event a form's
	// handler is (`New Event("Name")`); either is one of the platform's event dispatchers (ibEventDispatcher).
	// The named one is called through that module, so the module is taken now — and an unknown name is refused
	// now, where it was written, rather than ignored when the event comes.
	ibValue dispatcher;
	std::weak_ptr<ibProcUnit> runtime;
	ibValueEvent* named = nullptr;
	if (AsFunction(handler) != nullptr) {
		dispatcher = handler;
	}
	else if (handler.GetType() == ibValueTypes::TYPE_STRING || handler.ConvertToValue(named)) {
		const ibString procName = named != nullptr ? named->GetString() : handler.GetString();
		ibProcUnitState* const state = ibSession::GetPUState();
		ibProcUnit* const module = state != nullptr ? state->GetCurrentRunModule() : nullptr;
		if (module == nullptr)
			ibBackendCoreException::Error(_("Procedure or function \"%s\" not found!"), procName);
		module->FindMethod(procName, /*bError*/ true);
		runtime = module->weak_from_this();
		dispatcher = named != nullptr ? handler : ibValue::CreateObjectValue<ibValueEvent>(procName);
	}
	else {
		ibBackendCoreException::Error(_("The handler of event \"%s\" must be a function or a procedure name"), eventName);
	}

	if (m_eventSink == nullptr) {
		IID iid = IID_NULL;
		const ComPtr<ITypeInfo> sourceInfo = FindSourceInterface(m_dispatch.Get(), m_clsId, iid);
		if (sourceInfo == nullptr)
			ibBackendCoreException::Error(_("COM object \"%s\" has no events"), m_objectName);
		std::unique_ptr<ibComEventSink, ibComEventSinkRelease> sink(new ibComEventSink(iid, sourceInfo.Get(), m_objectName));
		if (!sink->Advise(m_dispatch.Get()))
			ibBackendCoreException::Error(_("COM object \"%s\" has no events"), m_objectName);
		m_eventSink = std::move(sink);
	}

	const DISPID dispid = m_eventSink->EventId(eventName);
	if (dispid == DISPID_UNKNOWN)
		ibBackendCoreException::Error(_("COM object \"%s\" has no event \"%s\""), m_objectName, eventName);
	m_eventSink->SetHandler(dispid, dispatcher, runtime);
}

void ibValueCOM::RemoveEventHandler(const ibString& eventName)
{
	if (m_eventSink == nullptr)
		return;
	m_eventSink->RemoveHandler(m_eventSink->EventId(eventName));
	if (m_eventSink->HasHandlers())
		return;
	m_eventSink.reset();
}

void ibValueCOM::DeliverQueuedEvents()
{
	if (m_eventSink != nullptr)
		m_eventSink->DeliverQueued();
}

#endif

//*********************************************************************************************************************
//*                                                      OLE Value                                                    *
//*********************************************************************************************************************

#ifdef __IBMSW__

IDispatch* ibValueCOM::DoCreateInstance()
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
		ibBackendCoreException::Error(_("COM object \"%s\" could not be created: %s"), m_objectName, ibSysErrorMsgStr(hr));

	return pDispatch;
}

ibValueCOM::ibValueCOM() : ibValueDynamicMembers(ibValueTypes::TYPE_OLE),
m_clsId(CLSID_NULL),
m_objectName(ibEmptyString)
{
	m_members.Bind(this, &ibValueCOM::FillMembers);
}

ibValueCOM::ibValueCOM(const CLSID& clsId, IDispatch* dispatch, const ibString& objectName) : ibValueDynamicMembers(ibValueTypes::TYPE_OLE),
m_clsId(clsId), m_dispatch(dispatch),
m_objectName(objectName)
{
	m_members.Bind(this, &ibValueCOM::FillMembers);
}
#else
ibValueCOM::ibValueCOM() : ibValueDynamicMembers(ibValueTypes::TYPE_OLE), m_objectName(ibEmptyString) {
	m_members.Bind(this, &ibValueCOM::FillMembers);
}
#endif

// Nothing is let go by hand: the sink disconnected (ibComEventSinkRelease), then the object (ComPtr) — the
// reverse of their declaration.
ibValueCOM::~ibValueCOM() = default;

bool ibValueCOM::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	const ibString& strOleName = paParams[0]->GetString();
	if (!Create(strOleName))
		ibBackendCoreException::Error(_("No COM class is registered as \"%s\""), strOleName);
	return true;
}

bool ibValueCOM::Create(const ibString& strOleName)
{
	// THE CARET'S WALK FOR COMPLETION STARTS NO COM SERVER: it builds `New COMObject("…")` to list its members,
	// and that must not launch the application named. Asked of the evaluation, not of the process — the
	// designer runs no other script, and the client's editors complete with the same walk (census, 2026-10-01).
	if (ibBackendException::IsEvalComplete())
		return true;

#ifdef __IBMSW__
	if (m_dispatch != nullptr)
		return false;

	// The thread is in a single-threaded apartment already, entered by its owner: a worker of the session pool
	// (workerPoolHeadless.cpp), or a client's main thread, where wx initialises OLE. An object and its events stay on
	// the thread that made them — the debugger's work comes to that thread too (RunInParkedSession, debugServer.cpp).
	HRESULT hr = ::CLSIDFromProgID(strOleName.wc_str(), &m_clsId);
	if (FAILED(hr))
		return false;

	m_objectName = strOleName;
	m_dispatch.Attach(DoCreateInstance());
	InvalidateNames();   // COM surface depends on the freshly created dispatch
	return m_dispatch != nullptr;
#else
	// The type is here so that a module which creates one compiles on every platform; the object is not.
	ibBackendCoreException::Error(_("COM objects are available on Windows only"));
#endif
}

bool ibValueCOM::CompareValueEQ(const ibVariant& cParam) const
{
#ifdef __IBMSW__
	const ibValueCOM* rhs = dynamic_cast<const ibValueCOM*>(cParam.GetRef());
	if (rhs != nullptr)
		return m_dispatch == rhs->m_dispatch;
#endif
	return false;
}

bool ibValueCOM::CompareValueNE(const ibVariant& cParam) const
{
	return !CompareValueEQ(cParam);
}

void ibValueCOM::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProc(ibT("AddEventHandler"), 2, ibT("AddEventHandler(eventName : string, handler : function | string)"), s_methodAddEventHandler);
	helper.AppendProc(ibT("RemoveEventHandler"), 1, ibT("RemoveEventHandler(eventName : string)"), s_methodRemoveEventHandler);

#ifdef __IBMSW__
	if (m_dispatch == nullptr)
		return;
	unsigned int count = 0;
	if (m_dispatch->GetTypeInfoCount(&count) != S_OK)
		return;
	for (unsigned int i = 0; i < count; i++) {

		HRESULT hr = S_OK;
		ComPtr<ITypeInfo> typeInfo; TYPEATTR* typeAttr = nullptr;

		hr = m_dispatch->GetTypeInfo(i, LOCALE_SYSTEM_DEFAULT, &typeInfo);
		if (hr != S_OK)
			continue;

		hr = typeInfo->GetTypeAttr(&typeAttr);
		if (hr != S_OK)
			continue;

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
	}
#endif
}

long ibValueCOM::FindMethod(const ibString& strMethodName) const
{
	if (strMethodName.IsSameAs(ibT("AddEventHandler"), false))
		return s_methodAddEventHandler;
	if (strMethodName.IsSameAs(ibT("RemoveEventHandler"), false))
		return s_methodRemoveEventHandler;
	return FindProp(strMethodName);
}

long ibValueCOM::FindProp(const ibString& strPropName) const
{
#ifdef __IBMSW__
	if (m_dispatch == nullptr)
		return wxNOT_FOUND;
	LPOLESTR name = const_cast<LPOLESTR>(strPropName.wc_str());
	DISPID dispid = DISPID_UNKNOWN;
	const HRESULT hr = m_dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &dispid);
	if (hr == DISP_E_UNKNOWNNAME || hr == DISP_E_MEMBERNOTFOUND)
		return wxNOT_FOUND;
	// Any other failure is the server's answer, not "no such member": one that rejected the call — busy, a dialog of its
	// own open — is named with its reason (2026-11-03: an Excel behind its activation wizard read as "field not found").
	if (FAILED(hr))
		ibBackendCoreException::Error(_("COM object \"%s\", member \"%s\": %s"), m_objectName, strPropName, ibSysErrorMsgStr(hr));
	m_memberNames[dispid] = strPropName;
	return dispid;
#else
	return wxNOT_FOUND;
#endif
}

#ifdef __IBMSW__

ibString ibValueCOM::MemberName(const long dispid) const
{
	const auto it = m_memberNames.find(dispid);
	return it != m_memberNames.end() ? it->second : ibString::Format(ibT("%ld"), dispid);
}

#endif

long ibValueCOM::GetNParams(const long lMethodNum) const
{
	if (lMethodNum == s_methodAddEventHandler)
		return 2;
	if (lMethodNum == s_methodRemoveEventHandler)
		return 1;
	return s_comArgsBound;
}

bool ibValueCOM::HasRetVal(const long lMethodNum) const
{
	return lMethodNum != s_methodAddEventHandler && lMethodNum != s_methodRemoveEventHandler;
}

bool ibValueCOM::GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const
{
#ifdef __IBMSW__
	// A HOLE IN THE CALL (`f(a, , c)`) — the interpreter asks for its default and this is the only moment
	// the object hears of it. Noted, and sent as the automation "missing" rather than as an Undefined.
	m_omittedArgs.push_back(lParamNum);
#endif
	return false;
}

#ifdef __IBMSW__

void ibValueCOM::Invoke(const long dispid, unsigned short flags, DISPPARAMS& params, VARIANT* result) const
{
	EXCEPINFO excepInfo = {};
	UINT argErr = 0;
	const HRESULT hr = m_dispatch->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, &excepInfo, &argErr);
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
			description = ibSysErrorMsgStr(excepInfo.scode != 0 ? excepInfo.scode : hr);
	}
	else if (hr == DISP_E_TYPEMISMATCH || hr == DISP_E_PARAMNOTFOUND) {
		description = ibString::Format(_("argument %d: %s"), static_cast<int>(params.cArgs - argErr), ibSysErrorMsgStr(hr));
	}
	else {
		description = ibSysErrorMsgStr(hr);
	}
	::SysFreeString(excepInfo.bstrSource);
	::SysFreeString(excepInfo.bstrDescription);
	::SysFreeString(excepInfo.bstrHelpFile);

	ibBackendCoreException::Error(_("COM object \"%s\", member \"%s\": %s"), m_objectName, MemberName(dispid), description);
}

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

bool ibValueCOM::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
#ifdef __IBMSW__
	if (m_dispatch == nullptr)
		return false;
	DeliverQueuedEvents();

	VARIANT oleVariant;
	FromValue(varPropVal, oleVariant);
	EXCEPINFO excepInfo = {};
	const HRESULT hr = PutProperty(m_dispatch.Get(), lPropNum, &oleVariant, &excepInfo);
	::VariantClear(&oleVariant);
	if (m_eventSink != nullptr)
		m_eventSink->RaisePendingError();
	if (FAILED(hr)) {
		if (hr == DISP_E_EXCEPTION && excepInfo.pfnDeferredFillIn != nullptr)
			excepInfo.pfnDeferredFillIn(&excepInfo);
		ibString description = FromBSTR(excepInfo.bstrDescription);
		if (description.IsEmpty())
			description = ibSysErrorMsgStr(hr);
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

bool ibValueCOM::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
#ifdef __IBMSW__
	if (m_dispatch == nullptr)
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

#ifdef __IBMSW__

const std::vector<bool>& ibValueCOM::ByRefArgs(const long dispid) const
{
	const auto known = m_byRefArgs.find(dispid);
	if (known != m_byRefArgs.end())
		return known->second;

	std::vector<bool>& byRef = m_byRefArgs[dispid];
	ComPtr<ITypeInfo> typeInfo;
	if (m_dispatch == nullptr || FAILED(m_dispatch->GetTypeInfo(0, LOCALE_USER_DEFAULT, &typeInfo)) || typeInfo == nullptr)
		return byRef;

	TYPEATTR* typeAttr = nullptr;
	if (SUCCEEDED(typeInfo->GetTypeAttr(&typeAttr)) && typeAttr != nullptr) {
		for (UINT f = 0; f < typeAttr->cFuncs && byRef.empty(); f++) {
			FUNCDESC* funcDesc = nullptr;
			if (FAILED(typeInfo->GetFuncDesc(f, &funcDesc)) || funcDesc == nullptr)
				continue;
			if (funcDesc->memid == dispid && (funcDesc->invkind == INVOKE_FUNC || funcDesc->invkind == INVOKE_PROPERTYGET)) {
				byRef.resize(funcDesc->cParams, false);
				// [out], or a pointer that declares no direction (a Visual Basic ByRef). A pointer declared [in]
				// only is not written back: Scripting.Dictionary.Add takes `[in] VARIANT* Item`, and the
				// array a script added came back into its variable as the dictionary's copy.
				for (SHORT p = 0; p < funcDesc->cParams; p++) {
					const ELEMDESC& param = funcDesc->lprgelemdescParam[p];
					const USHORT direction = param.paramdesc.wParamFlags & (PARAMFLAG_FIN | PARAMFLAG_FOUT);
					byRef[p] = (direction & PARAMFLAG_FOUT) != 0 || (param.tdesc.vt == VT_PTR && direction == 0);
				}
			}
			typeInfo->ReleaseFuncDesc(funcDesc);
		}
		typeInfo->ReleaseTypeAttr(typeAttr);
	}
	return byRef;
}

#endif

// The object's own two members are procedures (HasRetVal), and the interpreter calls a procedure here.
bool ibValueCOM::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
#ifdef __IBMSW__
	m_omittedArgs.clear();

	if (lMethodNum == s_methodAddEventHandler) {
		if (m_dispatch == nullptr)
			return false;
		AddEventHandler(paParams[0]->GetString(), *paParams[1]);
		return true;
	}
	if (lMethodNum == s_methodRemoveEventHandler) {
		RemoveEventHandler(paParams[0]->GetString());
		return true;
	}
#endif
	return false;
}

bool ibValueCOM::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
#ifdef __IBMSW__
	std::vector<long> omitted;
	omitted.swap(m_omittedArgs);

	if (m_dispatch == nullptr)
		return false;
	DeliverQueuedEvents();

	// rgvarg runs right to left: the first argument is the last VARIANT. An argument the member declares
	// [out] goes as a reference to a VARIANT of its own — and so does every argument of a member the object
	// describes no types for, the way a script engine passes its variables (VBScript does); the rest go by
	// value. What the object CHANGED there comes back to the caller's variable, and nothing else: an array it
	// only read stays the caller's Array.
	const std::vector<bool>& byRefDeclared = ByRefArgs(lMethodNum);
	const bool undescribed = byRefDeclared.empty();
	std::vector<VARIANT> args(lSizeArray), byRefValues(lSizeArray), sent(lSizeArray);
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
			if (undescribed || (arg < static_cast<long>(byRefDeclared.size()) && byRefDeclared[arg])) {
				FromValue(*paParams[arg], byRefValues[arg]);
				sent[arg] = byRefValues[arg];     // the bits as they went, to tell what the object changed
				slot.vt = VT_BYREF | VT_VARIANT;
				slot.pvarVal = &byRefValues[arg];
				byRef[lSizeArray - arg - 1] = true;
				continue;
			}
			FromValue(*paParams[arg], slot);
		}

		DISPPARAMS params = { lSizeArray > 0 ? args.data() : nullptr, nullptr, static_cast<UINT>(lSizeArray), 0 };
		Invoke(lMethodNum, DISPATCH_METHOD | DISPATCH_PROPERTYGET, params, &result);

		for (long arg = 0; arg < lSizeArray; arg++) {
			if (byRef[lSizeArray - arg - 1] && std::memcmp(&byRefValues[arg], &sent[arg], sizeof(VARIANT)) != 0)
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

#ifdef __IBMSW__

// `For Each` over the object: its _NewEnum, walked one VARIANT at a time.
class ibComEnumState : public ibValueIteratorState {
public:
	ibComEnumState(const ComPtr<IEnumVARIANT>& enumerator, const ibString& objectName)
		: m_enumerator(enumerator), m_objectName(objectName) {
	}
	bool MoveNext(ibValue& current) override {
		VARIANT item;
		::VariantInit(&item);
		ULONG fetched = 0;
		if (m_enumerator->Next(1, &item, &fetched) != S_OK || fetched == 0)
			return false;
		try {
			ibValueCOM::FromVariant(item, current, m_objectName);
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
	const ComPtr<IEnumVARIANT> m_enumerator;
	const ibString m_objectName;
};

#endif

std::shared_ptr<ibValueIteratorState> ibValueCOM::CreateIterator()
{
#ifdef __IBMSW__
	if (m_dispatch == nullptr)
		return nullptr;

	VARIANT result;
	::VariantInit(&result);
	DISPPARAMS params = { nullptr, nullptr, 0, 0 };
	EXCEPINFO excepInfo = {};
	const HRESULT hr = m_dispatch->Invoke(DISPID_NEWENUM, IID_NULL, LOCALE_USER_DEFAULT,
		DISPATCH_METHOD | DISPATCH_PROPERTYGET, &params, &result, &excepInfo, nullptr);
	::SysFreeString(excepInfo.bstrSource);
	::SysFreeString(excepInfo.bstrDescription);
	::SysFreeString(excepInfo.bstrHelpFile);

	ComPtr<IEnumVARIANT> enumerator;
	if (SUCCEEDED(hr) && (result.vt == VT_UNKNOWN || result.vt == VT_DISPATCH) && result.punkVal != nullptr)
		result.punkVal->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(enumerator.GetAddressOf()));
	::VariantClear(&result);

	if (enumerator == nullptr)
		ibBackendCoreException::Error(_("COM object \"%s\" cannot be walked with For Each"), m_objectName);
	return std::make_shared<ibComEnumState>(enumerator, m_objectName);
#else
	return nullptr;
#endif
}

void ibValueCOM::DispatchLinqMethod(ibLinqMethod method, ibValue& ret, ibValue** args, long n)
{
	// ⭐ THE SERVER'S OWN MEMBER WINS. The compiler emits every `.Sum(` or `.Select(` as a LINQ operator, and LINQ asks
	// for the enumerator first — but a COM object's names are its server's: Excel's `Range.Select()` selects, and
	// `WorksheetFunction.Sum(…)` adds. A name the object does not have stays LINQ over its _NewEnum (2026-11-03:
	// `WorksheetFunction.Sum` was refused as "cannot be walked with For Each").
	for (const ibLinqMethodInfo& info : GetLinqMethodTable()) {
		if (info.id != method)
			continue;
		const long own = FindMethod(info.name);
		if (own >= 0) {
			CallAsFunc(own, ret, args, n);
			return;
		}
		break;
	}
	ibValueDynamicMembers::DispatchLinqMethod(method, ret, args, n);
}

#pragma warning(pop)

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueCOM, "ComObject", value_to_clsid("VL_OLE"));
