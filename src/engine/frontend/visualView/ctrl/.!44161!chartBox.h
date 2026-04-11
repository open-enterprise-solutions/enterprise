#ifndef _CHARTBOX_H__
#define _CHARTBOX_H__

#include "window.h"

class ibValueChartBox : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueChartBox);
public:

	ibValueChartBox();

	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
