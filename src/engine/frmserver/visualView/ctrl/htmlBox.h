#ifndef  _HTMLBOX_H__
#define  _HTMLBOX_H__

#include "window.h"

class ibValueHTMLBox : public ibValueWindow {
	public:

	ibValueHTMLBox();

	//methods
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);       //method call

	// The page the code last set (SetPage) — the box's whole content.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	// Set from code only, never saved: the page lives as long as the form is open.
	wxString m_page;
};

#endif // ! _HTMLBOX_H__
