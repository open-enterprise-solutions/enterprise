#ifndef _DYNAMIC_BORDER_H__
#define _DYNAMIC_BORDER_H__

#include <wx/stattext.h>

class IDynamicBorder {
public:
	virtual bool AllowCalc() const { return true; };
	virtual wxStaticText *GetStaticText() const = 0;

	virtual void BeforeCalc() {}
	virtual void AfterCalc() {}
};

#endif 