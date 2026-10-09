#ifndef _CHARTBOX_H__
#define _CHARTBOX_H__

#include "window.h"

class ibValueChartBox : public ibValueWindow {
	public:

	ibValueChartBox();

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;
};

#endif
