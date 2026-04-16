////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : Minimal main frame for web server mode (daemon)
//	              Enables CreateNewForm() without GUI
////////////////////////////////////////////////////////////////////////////

#include "ibWebMainFrame.h"

#include "frontend/visualView/ctrl/form.h"

ibWebMainFrame* ibWebMainFrame::ms_instance = nullptr;

ibWebMainFrame::ibWebMainFrame()
{
}

ibWebMainFrame::~ibWebMainFrame()
{
}

ibBackendValueForm* ibWebMainFrame::CreateNewForm(
	const ibValueMetaObjectFormBase* creator,
	ibBackendControlFrame* ownerControl,
	ibSourceDataObject* srcObject,
	const ibUniqueKey& formGuid)
{
	ibControlFrame* control = dynamic_cast<ibControlFrame*>(ownerControl);
	return ibValue::CreateAndPrepareValueRef<ibValueForm>(
		creator, control, srcObject, formGuid);
}

void ibWebMainFrame::Initialize()
{
	if (ms_instance == nullptr) {
		ms_instance = new ibWebMainFrame();
	}
}

void ibWebMainFrame::Destroy()
{
	if (ms_instance != nullptr) {
		delete ms_instance;
		ms_instance = nullptr;
	}
}
