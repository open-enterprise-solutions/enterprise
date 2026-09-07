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

// ⭐⭐ AND THIS ONE IS THE OTHER KIND — IT TELLS, IT DOES NOT ASK. Every function above hands a form
// BACK, so a process with no windows cannot answer them and must say so. This one hands a form
// nothing but news: a record whose key floats over its dimensions has just been re-keyed, and any
// window still showing it under the old key needs to know. Where there are no windows the honest
// answer is "nobody was re-keyed" — false — and not a refusal, because nothing was asked for.
//
// 🛑 MEASURED 2026-09-07: it raised, and it took the WRITE with it. `RecordManager.Write()` calls
// this between storing the row and committing (informationRegisterObject.cpp), so filling an
// information register from a background job — no frame, by construction — failed on its first
// record with "context functions are not available: this is a server". Nothing about that write
// needed a window. The rule lives here rather than in a try/catch at the call site for the usual
// reason: the next caller of a TELL verb would have to remember, and would not.
bool ibBackendValueForm::UpdateFormUniqueKey(const ibUniqueKeyPair& guid)
{
	if (ibSession::CurrentFrame() != nullptr) return ibSession::CurrentFrame()->UpdateFormUniqueKey(guid);
	return false;   // no windows in this process — nothing showing the old key, nothing to re-key
}