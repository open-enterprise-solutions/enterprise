#ifndef _WINDOW_BASE_H_
#define _WINDOW_BASE_H_

#include "baseControl.h"

#define FORM_ACTION 1

class IValueWindow : public IValueControl
{
	wxDECLARE_ABSTRACT_CLASS(IValueWindow);

public:

	wxPoint m_pos = wxDefaultPosition;
	wxSize m_size = wxDefaultSize;
	wxSize m_minimum_size = wxDefaultSize;
	wxSize m_maximum_size = wxDefaultSize;
	wxFont m_font;

	wxColour m_fg = RGB(0, 120, 215);
	wxColour m_bg = RGB(240, 240, 240);

	long m_window_style = 0;
	long m_window_extra_style = 0;
	long m_center;

	wxString m_tooltip;
	bool m_context_menu;
	wxString m_context_help;
	bool m_enabled = true;
	bool m_visible = true;

public:

	IValueWindow();

	//attributes 
	virtual void SetAttribute(attributeArg_t &aParams, CValue &cVal) override; //установка атрибута

	//load & save object in control 
	virtual bool LoadData(CMemoryReader &reader);
	virtual bool SaveData(CMemoryWriter &writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

	virtual int GetComponentType() override { return COMPONENT_TYPE_WINDOW; }
	virtual bool IsItem() override { return false; }

protected:

	void UpdateWindow(wxWindow* window);
	void UpdateLabelSize(class IDynamicBorder *textCtrl);
};

#endif 