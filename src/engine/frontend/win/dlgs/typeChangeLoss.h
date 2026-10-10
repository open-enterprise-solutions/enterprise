#ifndef __TYPE_CHANGE_LOSS_H__
#define __TYPE_CHANGE_LOSS_H__

#include <wx/dialog.h>

#include "frontend/frontend.h"
#include "backend/query/typeChangeReport.h"

// Shown before any structure is written, and only when the report has a loss.
// Accept continues the update. Cancel leaves the database as it is.
class FRONTEND_API ibDialogTypeChangeLoss : public wxDialog
{
	ibDialogTypeChangeLoss(const ibTypeChangeReport& report, wxWindow* parent);

public:
	static bool Confirm(const ibTypeChangeReport& report, wxWindow* parent)
	{
		ibDialogTypeChangeLoss dlg(report, parent);
		return dlg.ShowModal() == wxID_OK;
	}
};

#endif
