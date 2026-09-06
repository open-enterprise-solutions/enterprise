#include "backend_form.h"
#include "backend/backend_exception.h"
#include "backend/backend_mainFrame.h"
#include "backend/session/session.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ibBackendValueForm* ibBackendValueForm::CreateNewForm(
	const ibValueMetaObjectFormBase* creator,
	ibBackendControlFrame* ownerControl,
	ibSourceDataObject* srcObject,
	const ibUniqueKey& formGuid
)
{
	if (ibSession::CurrentFrame() != nullptr) {
		ibBackendValueForm* createdForm = ibSession::CurrentFrame()->CreateNewForm(
			creator,
			ownerControl,
			srcObject,
			formGuid
		);

		if (createdForm == nullptr) {
			ibBackendFormException::Error(_("a form this frontend library cannot build"));
			return nullptr;
		}
		return createdForm;
	}

	ibBackendFormException::Error();
	return nullptr;
}

ibUniqueKey ibBackendValueForm::CreateFormUniqueKey(ibBackendControlFrame* ownerControl, ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid)
{
	if (ibSession::CurrentFrame() != nullptr)  return ibSession::CurrentFrame()->CreateFormUniqueKey(ownerControl, sourceObject, formGuid);
	ibBackendFormException::Error();
	return ibUniqueKey();
}

ibBackendValueForm* ibBackendValueForm::FindFormByUniqueKey(ibBackendControlFrame* ownerControl, ibSourceDataObject* sourceObject, const ibUniqueKey& formGuid)
{
	if (ibSession::CurrentFrame() != nullptr) return ibSession::CurrentFrame()->FindFormByUniqueKey(ownerControl, sourceObject, formGuid);
	ibBackendFormException::Error();
	return nullptr;
}

ibBackendValueForm* ibBackendValueForm::FindFormByUniqueKey(const ibUniqueKey& guid)
{
	if (ibSession::CurrentFrame() != nullptr)  return ibSession::CurrentFrame()->FindFormByUniqueKey(guid);
	ibBackendFormException::Error();
	return nullptr;
}

ibBackendValueForm* ibBackendValueForm::FindFormByControlUniqueKey(const ibUniqueKey& guid)
{
	if (ibSession::CurrentFrame() != nullptr)  return ibSession::CurrentFrame()->FindFormByControlUniqueKey(guid);
	ibBackendFormException::Error();
	return nullptr;
}

// ⚠ REACHING FOR A FORM WHERE THERE IS NO FRAME RAISES, AND THAT IS THE DESIGN — a server has no
// windows, and code that asks one for a form has asked the wrong process. It is not softened to a
// null: a silent nothing here would let form-driven logic run half-done on the server and answer
// differently there, which is the failure nobody audits (Max, 2026-09-06: *"accessing a form on the
// server throws — that is intended"*).
//
// ⚠ AND IT IS NOT CAUGHT ON THE WAY UP. Callers ask plainly; the refusal reaches whoever started the
// code and is reported there, which is the only place that can say which line asked for a window in
// a process that has none. A leaf that swallows it makes a null mean two different things at once.
ibBackendValueForm* ibBackendValueForm::FindFormBySourceUniqueKey(const ibUniqueKey& guid)
{
	if (ibSession::CurrentFrame() != nullptr)  return ibSession::CurrentFrame()->FindFormBySourceUniqueKey(guid);
	ibBackendFormException::Error();
	return nullptr;
}

bool ibBackendValueForm::UpdateFormUniqueKey(const ibUniqueKeyPair& guid)
{
	if (ibSession::CurrentFrame() != nullptr) return ibSession::CurrentFrame()->UpdateFormUniqueKey(guid);
	ibBackendFormException::Error();
	return false;
}