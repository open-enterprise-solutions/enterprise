#ifndef _FRAME_INT_H__
#define _FRAME_INT_H__

#include "backend/uniqueKey.h"

class BACKEND_API IBackendMetaDocument {
public:
	virtual ~IBackendMetaDocument() {}
	virtual class IMetaObject* GetMetaObject() const = 0;
};

class BACKEND_API IBackendControlFrame {
public:
#if !wxUSE_EXTENDED_RTTI
	virtual wxClassInfo* GetClassInfo() const;
	// RTTI information, usually declared by wxDECLARE_DYNAMIC_CLASS() or
	// similar, but done manually for the hierarchy root. Note that it's public
	// for compatibility reasons, but shouldn't be accessed directly.
	static wxClassInfo ms_classInfo;
#endif
	virtual ~IBackendControlFrame() {}
	virtual bool GetControlValue(CValue& pvarControlVal) const = 0;
	virtual Guid GetControlGuid() const = 0;
};

class BACKEND_API IBackendValueForm : public IBackendValue {
public:
	static IBackendValueForm* CreateNewForm(IBackendControlFrame* ownerControl = nullptr, class IMetaObjectForm* metaForm = nullptr,
		class ISourceDataObject* ownerSrc = nullptr, const CUniqueKey& formGuid = wxNullUniqueKey, bool readOnly = false);

	static IBackendValueForm* FindFormByUniqueKey(const CUniqueKey& guid);
	static bool UpdateFormUniqueKey(const CUniquePairKey& guid);

	///////////////////////////////////////////////////////////////////////////
	virtual ~IBackendValueForm() {}
	///////////////////////////////////////////////////////////////////////////

	virtual bool LoadForm(const wxMemoryBuffer& formData) = 0;
	virtual wxMemoryBuffer SaveForm() = 0;

	///////////////////////////////////////////////////////////////////////////

	virtual void BuildForm(const form_identifier_t& formType) = 0;
	virtual bool InitializeFormModule() = 0;

	//choice
	virtual void NotifyChoice(CValue& vSelected) = 0;

	virtual void ActivateForm() = 0;
	virtual void UpdateForm() = 0;
	virtual bool CloseForm() = 0;
	virtual void HelpForm() = 0;

	virtual bool GenerateForm(class IRecordDataObjectRef* obj) const = 0;

	virtual void Modify(bool modify = true) = 0;
	virtual bool IsModified() const = 0;

	//shown form 
	virtual bool IsShown() const = 0;
	virtual void ShowForm(IBackendMetaDocument* doc = nullptr, bool demoRun = false) = 0;
};

namespace formWrapper {
	namespace inl {
		inline CValue* cast_value(IBackendControlFrame* form) {
			return dynamic_cast<CValue*>(form);
		}
		inline CValue* cast_value(IBackendValue* form) {
			return form ? form->GetImplValueRef() : nullptr;
		}
	}
};

#endif