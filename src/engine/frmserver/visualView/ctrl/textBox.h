#ifndef _TEXTBOX_H__
#define _TEXTBOX_H__

#include "window.h"

constexpr ibClassID g_controlTextBoxCLSID = control_to_clsid("CT_TEXT");

// A TEXT BOX — a multi-line text area on a form. It holds no text of its own: the text is the client's, as it
// was the editor widget's, so on the server it is a window like any other.
class ibValueTextBox : public ibValueWindow {
	public:

	ibValueTextBox();

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;
};

#endif
