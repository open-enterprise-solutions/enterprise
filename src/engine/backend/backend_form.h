#ifndef _FRAME_INT_H__
#define _FRAME_INT_H__

#include "backend/uniqueKey.h"
#include "backend/createRequest.h"   // ibFormRequest — what a form is MADE with
#include "backend/backend_command.h"   // ibBackendCommandSender — the form is the command SOURCE (server contract)
#include "backend/backend_exception.h" // ibBackendFormException — what a frameless lookup raises

///////////////////////////////////////////////////
class BACKEND_API ibBackendValueForm;
template <class T> class ibFormPtr;   // below — the owning reference a form is handed out by
///////////////////////////////////////////////////

// ⭐⭐ A FORM TO TELL, RATHER THAN A FORM TO USE — and the two are different questions that happened
// to share one call. Asking for a form RAISES where this process has none, and that is correct: code
// that wants to open, fill or read a form has asked the wrong process, and it must hear so.
//
// But a great deal of code asks only in order to NOTIFY: an object marking itself modified so an
// open window shows dirty, a write telling its form that the row it is showing has changed. For
// those, "there are no forms in this process" is not a failure of anything — it is the ordinary
// state of every write that is not somebody's window, and the operation must go through regardless.
// Wrap the ask in this and the answer is simply "nobody to tell".
//
// ⚠ IT CATCHES ONE TYPE AND NOTHING ELSE. A form that EXISTS and throws while answering is a real
// failure and still travels; only the frameless refusal is absorbed. That is the entire reason the
// refusal has a type of its own (Max, 2026-09-06: *"the form itself should throw an exception, that
// is normal - you swallow it in writing modifiedness; but when you WANT to get it, you see the
// error"*).
//
// 🛑⭐ AND WHERE THERE ARE NO WINDOWS IT IS NOT ASKED AT ALL. The refusal was the answer on every
// frameless write — a posting pass, a background job, codeRunner, a script run from MCP — and it came
// as a THROWN exception, once per changed field: an object marked itself modified 20 000 times and the
// process threw and caught 20 000 exceptions to learn 20 000 times that nobody was watching (measured
// 2026-09-24: 70 010 of them in one write bench, the most of its time). Whether this process shows
// windows is a question with a plain answer; asking it first leaves the catch for what it is for.
BACKEND_API bool ibSessionHasFrame();

template <class Ask>
inline ibBackendValueForm* ibFormToNotify(Ask&& ask)
{
	if (!ibSessionHasFrame())
		return nullptr;   // no windows in this process — nothing open, nobody to tell
	try {
		return ask();
	}
	catch (const ibBackendFormException&) {
		return nullptr;
	}
}

// ibBackendFormAttribute moved to backend_type.h (lives with the type-source
// factory + ibSourceDataType it depends on).

class BACKEND_API ibBackendMetaDocument {
public:
	virtual ~ibBackendMetaDocument() {}
	virtual const class ibValueMetaObject* GetMetaObject() const = 0;
};

class BACKEND_API ibBackendControlFrame {
public:

	virtual ~ibBackendControlFrame() {}

	virtual bool GetControlValue(ibValue& pvarControlVal) const = 0;
	virtual ibGuid GetControlGuid() const = 0;

	virtual ibBackendValueForm* GetBackendForm() const { return nullptr; }

	// Get reference class
	virtual ibClassID GetClassType() const = 0;

	// Counter reference
	virtual void ControlIncrRef() = 0;
	virtual void ControlDecrRef() = 0;
};

class BACKEND_API ibBackendValueForm : public ibBackendValue, public ibBackendCommandSender {
public:

	// ibBackendCommandSender — the form IS the command SOURCE, declared HERE (server-side) so a headless caller
	// (the web server / daemon / codeRunner holding a bare ibBackendValueForm*) can start the command walk and run a
	// command with NO front-end. PURE: every concrete form (desktop, web) must vend its commands.
	virtual bool GetCommandByHop(const ibCommandHop& hop, ibValue& out) override = 0;

#pragma region _frontend_call_h__

	// Form entry creator — born owned (ibFormPtr, below)
	static ibFormPtr<ibBackendValueForm> CreateNewForm(const ibFormRequest& request = ibFormRequest(), const class ibValueMetaObjectFormBase* creator = nullptr, ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr);

	static ibUniqueKey CreateFormUniqueKey(ibBackendControlFrame* ownerControl,
		ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	static ibBackendValueForm* FindFormByUniqueKey(ibBackendControlFrame* ownerControl,
		ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid);

	static ibBackendValueForm* FindFormByUniqueKey(const ibUniqueKey& guid);
	static ibBackendValueForm* FindFormByControlUniqueKey(const ibUniqueKey& guid);
	static ibBackendValueForm* FindFormBySourceUniqueKey(const ibUniqueKey& guid);

	static bool UpdateFormUniqueKey(const ibUniqueKeyPair& guid);

#pragma endregion 

	///////////////////////////////////////////////////////////////////////////
	virtual ~ibBackendValueForm() {}
	///////////////////////////////////////////////////////////////////////////

	// Counter reference — what ibFormPtr holds the form by; the form decides what a reference is.
	virtual void FormIncrRef() = 0;
	virtual void FormDecrRef() = 0;

	virtual bool LoadForm(const wxMemoryBuffer& data) = 0;
	virtual bool SaveForm(wxMemoryBuffer &data) const = 0;

	///////////////////////////////////////////////////////////////////////////

	virtual ibSourceDataObject* GetSourceObject() const = 0;
	virtual const ibValueMetaObjectFormBase* GetFormMetaObject() const = 0;

	///////////////////////////////////////////////////////////////////////////

	virtual void BuildForm(const ibFormID& formType) = 0;
	virtual bool InitializeFormModule() = 0;

	//notify
	virtual void NotifyCreate(const ibValue& vCreated) = 0;
	virtual void NotifyChange(const ibValue& vChanged) = 0;
	virtual void NotifyDelete(const ibValue& vChanged) = 0;

	virtual void NotifyChoice(ibValue& vSelected) = 0;

	//form event
	virtual void ActivateForm() = 0;
	virtual void UpdateForm() = 0;
	virtual bool CloseForm(bool force = false) = 0;
	virtual void HelpForm() = 0;

	virtual bool GenerateForm(class ibValueRecordDataObjectRef* obj) const = 0;
	// Opening stays what it always was: everything was said when the form was made.
	virtual void ShowForm(ibBackendMetaDocument* doc = nullptr, bool createContext = true) = 0;

	//set & get modify 
	virtual void Modify(bool modify = true) = 0;
	virtual bool IsModified() const = 0;

	//shown form 
	virtual bool IsShown() const = 0;

	//support close form
	virtual void CloseOnChoice(bool close = true) = 0;
	virtual bool IsCloseOnChoice() const = 0;

	virtual void CloseOnOwnerClose(bool close = true) = 0;
	virtual bool IsCloseOnOwnerClose() const = 0;

	// ⭐ WHAT THIS FORM WAS MADE WITH — a READ, and only a read. The form is born with it (it arrives as
	// an argument all the way into the constructor), and this is how whoever holds the form asks it
	// back: "the server form is available everywhere" (Max, 2026-09-23).
	//
	// 🛑 AND THERE IS NO SETTER BESIDE IT, deliberately. A setter would mean a form that exists for a
	// moment not knowing what it was asked for, and a caller who has to remember to say so.
	//
	// Its first reader is the list's Add: a row created in a list narrowed to one owner must be born
	// with that owner, or it vanishes from the list the moment it is written.
	virtual const ibFormRequest& GetFormRequest() const = 0;
};

// ----------------------------------------------------------------------------
// ibFormPtr<T>: an owning reference to a FORM — ibSourcePtr's twin for the form interface. A form is not an
// ibValue, so it cannot sit in an ibValuePtr; it holds through FormIncrRef / FormDecrRef, which the form answers
// for itself (the desktop form: its value's refcount). What MAKES a form hands out one: a new form is born
// owned, so what happens to it while it is being made — its module run, the manager's FormGetProcessing —
// happens to a form somebody holds, and letting go of a reference cannot delete it.
//
// 🛑 NO IMPLICIT T*. A raw pointer taken from a returned holder outlives the holder and dangles without a word;
// Get() says it out loud, and a caller that must keep the form keeps the holder.
// ----------------------------------------------------------------------------

template <class T>
class ibFormPtr {
public:

	constexpr ibFormPtr() = default;
	constexpr ibFormPtr(nullptr_t) {}

	explicit ibFormPtr(T* ptr) { Bind(ptr); }

	ibFormPtr(const ibFormPtr& to_copy) { Bind(to_copy.m_ptr); }

	~ibFormPtr() { Reset(); }

	ibFormPtr& operator = (const ibFormPtr& other) {
		if (m_ptr != other.m_ptr) { Reset(); Bind(other.m_ptr); }
		return *this;
	}

	inline T* operator->() const { return m_ptr; }
	inline T* Get() const { return m_ptr; }
	inline explicit operator bool() const noexcept { return m_ptr != nullptr; }

	// …and into a script value, which holds it the same way.
	operator ibValue() const { return ibValue(static_cast<ibBackendValue*>(m_ptr)); }

private:

	void Bind(T* ptr) {
		m_ptr = ptr;
		if (m_ptr != nullptr) m_ptr->FormIncrRef();
	}

	void Reset() {
		T* const ptr = m_ptr;
		m_ptr = nullptr;
		if (ptr != nullptr) ptr->FormDecrRef();
	}

	T* m_ptr = nullptr;
};

namespace formWrapper {
	namespace inl {
		inline ibValue* cast_value(ibBackendControlFrame* form) {
			return dynamic_cast<ibValue*>(form);
		}
		inline ibValue* cast_value(ibBackendValue* form) {
			return form ? form->GetImplValueRef() : nullptr;
		}
		template <class T>
		inline ibValue* cast_value(const ibFormPtr<T>& form) {
			return form ? form->GetImplValueRef() : nullptr;
		}
	}
};

#endif