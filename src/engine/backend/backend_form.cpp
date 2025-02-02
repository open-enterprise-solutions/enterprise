#include "backend_form.h"
#include "backend/systemManager/systemManager.h"
#include "backend/backend_mainFrame.h"

#if !wxUSE_EXTENDED_RTTI
wxClassInfo IBackendControlFrame::ms_classInfo(wxT("IBackendControlFrame"), 0, 0,
	(int)sizeof(wxObject),
	(wxObjectConstructorFn)nullptr);

wxClassInfo* IBackendControlFrame::GetClassInfo() const {
	return &IBackendControlFrame::ms_classInfo;
}
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IBackendValueForm* IBackendValueForm::CreateNewForm(
	IBackendControlFrame* ownerControl,
	IMetaObjectForm* metaForm,
	ISourceDataObject* ownerSrc,
	const CUniqueKey& formGuid, bool readOnly
)
{
	if (backend_mainFrame != nullptr) {
		IBackendValueForm *createdForm = backend_mainFrame->CreateNewForm(
			ownerControl,
			metaForm,
			ownerSrc,
			formGuid, readOnly
		);
		
		if (createdForm == nullptr) {
			CSystemFunction::Raise(_("Context functions are not available in this frontend library!"));
			return nullptr;
		}
		return createdForm;
	}

	CSystemFunction::Raise(_("Context functions are not available!"));
	return nullptr;
}

IBackendValueForm* IBackendValueForm::FindFormByUniqueKey(const CUniqueKey& guid)
{
	if (backend_mainFrame != nullptr) {
		return backend_mainFrame->FindFormByUniqueKey(guid);
	}

	CSystemFunction::Raise(_("Context functions are not available!"));
	return nullptr;
}

bool IBackendValueForm::UpdateFormUniqueKey(const CUniquePairKey& guid)
{
	if (backend_mainFrame != nullptr) {
		return backend_mainFrame->UpdateFormUniqueKey(guid);
	}

	CSystemFunction::Raise(_("Context functions are not available!"));
	return false;
}