#ifndef _FORM_PRINTOUT_H__
#define _FORM_PRINTOUT_H__

#include <wx/print.h>
#include "frontend/visualView/visualInterface.h"

class CFormPrintout : public wxPrintout
{
	wxMemoryDC m_formMemoryDC;
	IVisualHost *m_visualHost;

public:
	CFormPrintout(IVisualHost *visualHost) : wxPrintout(visualHost->GetLabel()),
		m_visualHost(visualHost)
	{
		// save the display before it is clobbered by the print preview
		wxBitmap bitmap(visualHost->GetSize().GetX(), visualHost->GetSize().GetY());
		m_formMemoryDC.SelectObject(bitmap);

		wxClientDC visualHostDC(visualHost);

		m_formMemoryDC.Blit(0, 0, 5000, 5000,
			&visualHostDC, 0, 0);
	}

	bool OnPrintPage(int PageNum)
	{
		// copy saved dispay to printer DC
		GetDC()->StretchBlit(0, 0, 5000, 5000,
			&m_formMemoryDC, 0, 0, m_visualHost->GetSize().GetX(), m_visualHost->GetSize().GetY());

		return true;
	}
};

#endif 