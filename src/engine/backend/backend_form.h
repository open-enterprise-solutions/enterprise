#ifndef _FRAME_INT_H__
#define _FRAME_INT_H__

#include "backend/uniqueKey.h"

///////////////////////////////////////////////////
class BACKEND_API ibBackendValueForm;
///////////////////////////////////////////////////

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

class BACKEND_API ibBackendValueForm : public ibBackendValue {
public:

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