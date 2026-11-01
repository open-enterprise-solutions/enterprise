#ifndef __VALUE_OLE_H__
#define __VALUE_OLE_H__

#include "backend/compiler/value.h"

#include <map>
#include <memory>
#include <vector>

#ifdef __WXMSW__
class ibComEventSink;
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
// ⭐ THE VALUES CROSS BOTH WAYS by kind (FromVariant / ToVariant): numbers of every width, CY and DECIMAL
// exactly; strings; dates as wall-clock parts; arrays as SAFEARRAYs; another object as its IDispatch. A
// value with no COM spelling is REFUSED with its type named, never passed as something else.
//
// ⭐ EVENTS: `obj.AddEventHandler("Name", Function(…) {…})` advises a sink on the object's default source
// interface; the handler runs on the thread that owns the object (see ibComEventSink).
class BACKEND_API ibValueOLE :
	public ibValueDynamicMembers {
public:
	ibString m_objectName;
#ifdef __WXMSW__
	IDispatch* m_dispatch = nullptr;
	IStream* m_streamDispatch = nullptr;
	IDispatch* m_currentDispatch = nullptr;
	CLSID m_clsId;
#endif
private:
#ifdef __WXMSW__
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

	mutable std::map<long, ibString> m_memberNames;
	mutable std::map<long, std::vector<bool>> m_byRefArgs;

	// The holes of the call in progress (`f(a, , c)`): the slots the interpreter asked a default for.
	mutable std::vector<long> m_omittedArgs;

	ibComEventSink* m_eventSink = nullptr;
#endif
	friend class ibDebuggerServer;
public:

	// NOT transferable across sessions: a COM interface pointer is apartment-bound
	// (that is what the STA stream marshalling below exists for), and the object
	// behind it is external and mutable. Handing one to a job's own thread would
	// be a cross-apartment call nobody marshalled.
	virtual bool IsTransferable() const override { return false; }

	//STA
	static void CreateStreamForDispatch();
	static void ReleaseStreamForDispatch();

	static void ReleaseComObjects();

	//MTA
	static void GetInterfaceAndReleaseStream();

public:

#ifdef __WXMSW__
	IDispatch* GetDispatch() const {
		return m_dispatch;
	}

	// The two conversions, public for the event sink and the iterator (both in valueOLE.cpp).
	// `objectName` names the object in a refusal. ToVariant writes into a VARIANT the caller cleared.
	static void FromVariant(const VARIANT& oleVariant, ibValue& value, const ibString& objectName);
	static void ToVariant(const ibValue& value, VARIANT& oleVariant);
#endif

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual long FindMethod(const ibString& strMethodName) const override;
	virtual long FindProp(const ibString& strPropName) const override;

	// A DISPID is not an index into the member table, so the object answers these itself.
	virtual long GetNParams(const long lMethodNum) const override;
	virtual bool HasRetVal(const long lMethodNum) const override;
	virtual bool GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const override;

	virtual bool IsPropReadable(const long lPropNum) const {
		return true;
	}
	virtual bool IsPropWritable(const long lPropNum) const {
		return true;
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);//setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);//attribute value

	//WORK AS AN AGGREGATE OBJECT
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

	// `For Each x In obj` — the object's own enumerator (DISPID_NEWENUM).
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator() override;

#ifdef __WXMSW__
	ibValueOLE();
private:
	ibValueOLE(const CLSID& clsId, IDispatch* dispatch, const ibString& objectName);
public:
#else
	ibValueOLE();
#endif

	virtual ~ibValueOLE();

	bool Create(const ibString& strOleName);

	virtual bool Init() {
		return false;
	}

	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual ibString GetString() const { return m_objectName; }

	//operator '=='
	virtual bool CompareValueEQ(const ibVariant& cParam) const {
#ifdef __WXMSW__
		ibValueOLE* m_pValueOLE = dynamic_cast<ibValueOLE*>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch == m_pValueOLE->m_dispatch;
#endif
		return false;
	}

	//operator '!='
	virtual bool CompareValueNE(const ibVariant& cParam) const {
#ifdef __WXMSW__
		ibValueOLE* m_pValueOLE = dynamic_cast<ibValueOLE*>(cParam.GetRef());
		if (m_pValueOLE) return m_dispatch != m_pValueOLE->m_dispatch;
#endif
		return true;
	}

	//check is empty
	virtual bool IsEmpty() const {
		return false;
	}

};

#endif
