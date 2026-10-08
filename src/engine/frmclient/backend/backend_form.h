#ifndef _FRMCLIENT_BACKEND_FORM_H__
#define _FRMCLIENT_BACKEND_FORM_H__

// THE FORM'S CONTRACTS — the engine's (backend/backend_form.h) as far as a value cell of the client's settings windows
// stands on one: a control that holds a value. The form itself, its creation and its keys are the server's.

#include "frmclient/backend/compiler/value.h"
#include "core/guid.h"

class ibBackendValueForm;

class FRMCLIENT_API ibBackendControlFrame {
public:
	virtual ~ibBackendControlFrame() {}
	virtual bool GetControlValue(ibValue& pvarControlVal) const = 0;
	virtual ibGuid GetControlGuid() const = 0;
	virtual ibBackendValueForm* GetBackendForm() const { return nullptr; }
	virtual ibClassID GetClassType() const = 0;
	virtual void ControlIncrRef() = 0;
	virtual void ControlDecrRef() = 0;
};

#endif
