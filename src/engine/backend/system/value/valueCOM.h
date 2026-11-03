#ifndef __VALUE_COM_H__
#define __VALUE_COM_H__

#include "backend/compiler/value.h"

#include <map>
#include <memory>
#include <vector>

#ifdef __IBMSW__
#include <wrl/client.h>     // Microsoft::WRL::ComPtr — an interface pointer that releases itself

class ibComEventSink;

// Lets a sink go DISCONNECTED: the connection point holds it too, so a Release alone would not end it.
struct BACKEND_API ibComEventSinkRelease {
	void operator()(ibComEventSink* sink) const;
};
#endif

// ⭐⭐ AN OBJECT OF ANOTHER PROGRAM, REACHED BY NAME — `New COMObject("ADODB.Connection")`. Late binding
// through IDispatch: a member is looked up when it is used (GetIDsOfNames) and called through Invoke, so
// nothing about the object has to be known when the module is compiled.
//
// ⭐ THE TYPE IS REGISTERED ON EVERY PLATFORM, and the object is made only on Windows. A configuration that
// drives a scanner or a fiscal register through COM is the same configuration on a Mac: its modules have
// to compile there, and a module that never runs the line must not be refused for containing it. Creating
// one elsewhere raises the Windows-only refusal — at `New`, never at compile time.
//
// ⭐ THE VALUES CROSS BOTH WAYS by kind (FromVariant / FromValue): numbers of every width, CY and DECIMAL
// exactly; strings; dates as wall-clock parts; another object as its IDispatch. An array comes back as a
// ComSafeArray (valueSafeArray.h), and goes as one — an Array as VARIANTs, BinaryData as bytes. A value with no
// COM spelling is REFUSED with its type named, never passed as something else.
//
// ⭐ EVENTS: `obj.AddEventHandler("Name", Function(…) {…})` — or the name of a procedure of the module that
// subscribes — advises a sink on the object's default source interface; the handler runs on the thread that
// owns the object (see ibComEventSink).
class BACKEND_API ibValueCOM :
	public ibValueDynamicMembers {
public:

	ibValueCOM();
	virtual ~ibValueCOM();

	virtual bool Init() override {
		return false;
	}
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual ibString GetString() const override { return m_objectName; }

	//operator '=='
	virtual bool CompareValueEQ(const ibVariant& cParam) const override;
	//operator '!='
	virtual bool CompareValueNE(const ibVariant& cParam) const override;

	//check is empty
	virtual bool IsEmpty() const override {
		return false;
	}

	// NOT transferable across sessions: a COM interface pointer is apartment-bound,
	// and the object behind it is external and mutable. Handing one to a job's own
	// thread would be a cross-apartment call nobody marshalled.
	virtual bool IsTransferable() const override { return false; }

	virtual long FindMethod(const ibString& strMethodName) const override;
	virtual long FindProp(const ibString& strPropName) const override;

	// A DISPID is not an index into the member table, so the object answers these itself.
	virtual long GetNParams(const long lMethodNum) const override;
	virtual bool HasRetVal(const long lMethodNum) const override;
	virtual bool GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const override;

	virtual bool IsPropReadable(const long lPropNum) const override {
		return true;
	}
	virtual bool IsPropWritable(const long lPropNum) const override {
		return true;
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;//setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;//attribute value

	//WORK AS AN AGGREGATE OBJECT
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// `For Each x In obj` — the object's own enumerator (DISPID_NEWENUM).
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;
	// The server's own member wins over a LINQ operator of its name; any other is LINQ over the enumerator.
	virtual void DispatchLinqMethod(ibLinqMethod method, ibValue& ret, ibValue** args, long n) override;

	bool Create(const ibString& strOleName);

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

#ifdef __IBMSW__
	// The two conversions, public for the event sink and the iterator (both in valueCOM.cpp).
	// `objectName` names the object in a refusal. FromValue writes into a VARIANT the caller clears.
	static void FromVariant(const VARIANT& oleVariant, ibValue& value, const ibString& objectName);
	static void FromValue(const ibValue& value, VARIANT& oleVariant);
#endif

private:

#ifdef __IBMSW__
	ibValueCOM(const CLSID& clsId, IDispatch* dispatch, const ibString& objectName);

	IDispatch* DoCreateInstance();

	// The member's name by its DISPID, as the script spelled it — for the refusal that names it.
	ibString MemberName(const long dispid) const;

	// Which arguments of a member are written back (declared [out] or by reference in its type
	// information). Asked once per member and kept.
	const std::vector<bool>& ByRefArgs(const long dispid) const;

	// One Invoke, refused with the object and the member named when it fails.
	void Invoke(const long dispid, unsigned short flags, DISPPARAMS& params, VARIANT* result) const;

	void AddEventHandler(const ibString& eventName, const ibValue& handler);
	void RemoveEventHandler(const ibString& eventName);

	// An event that arrived on another thread is handed over here, on the owner's, before the next call.
	void DeliverQueuedEvents();

	CLSID m_clsId;
	Microsoft::WRL::ComPtr<IDispatch> m_dispatch;

	std::unique_ptr<ibComEventSink, ibComEventSinkRelease> m_eventSink;

	mutable std::map<long, ibString> m_memberNames;
	mutable std::map<long, std::vector<bool>> m_byRefArgs;

	// The holes of the call in progress (`f(a, , c)`): the slots the interpreter asked a default for.
	mutable std::vector<long> m_omittedArgs;
#endif

	ibString m_objectName;
};

#endif
