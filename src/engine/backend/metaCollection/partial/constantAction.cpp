#include "constant.h"

enum
{
	eDefActionAndClose = 1,
	eSave,
};


ibValueRecordDataObjectConstant::ibStandardCommandSet ibValueRecordDataObjectConstant::GetStandardCommands(const ibFormID& formType)
{
	ibStandardCommandSet catalogActions(this);
	catalogActions.AddAction(wxT("SaveAndClose"), _("Save and close"), g_picSaveCLSID, true, eDefActionAndClose);
	catalogActions.AddAction(wxT("Save"), _("Save"), g_picSaveCLSID, true, eSave);
	return catalogActions;
}

void ibValueRecordDataObjectConstant::CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm)
{
	switch (lNumAction)
	{
	case eDefActionAndClose:
		if (SetConstValue(m_constValue))
			srcForm->CloseForm();
		break;
	case eSave: 
		SetConstValue(m_constValue); 
		break;
	}
}