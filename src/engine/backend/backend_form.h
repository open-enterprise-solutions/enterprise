#ifndef _FRAME_INT_H__
#define _FRAME_INT_H__

#include "backend/uniqueKey.h"
#include "backend/backend_command.h"   // ibBackendCommandSender — the form is the command SOURCE (server contract)
#include "backend/backend_exception.h" // ibBackendFormException — what a frameless lookup raises

///////////////////////////////////////////////////
class BACKEND_API ibBackendValueForm;
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
template <class Ask>
inline ibBackendValueForm* ibFormToNotify(Ask&& ask)
{
	try {
		return ask();
	}
	catch (const ibBackendFormException&) {
		return nullptr;   // no windows in this process — nothing open, nobody to tell
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

	// Form entry creator 
	static ibBackendValueForm* CreateNewForm(const class ibValueMetaObjectFormBase* creator = nullptr, ibBackendControlFrame* ownerControl = nullptr,
		class ibSourceDataObject* srcObject = nullptr, const ibUniqueKey& formGuid = wxNullUniqueKey);

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
};

namespace formWrapper {
	namespace inl {
		inline ibValue* cast_value(ibBackendControlFrame* form) {
			return dynamic_cast<ibValue*>(form);
		}
		inline ibValue* cast_value(ibBackendValue* form) {
			return form ? form->GetImplValueRef() : nullptr;
		}
	}
};

#endif