#ifndef FONT_COLOR_SETTINGS_PANEL_H
#define FONT_COLOR_SETTINGS_PANEL_H

/////////////////////////////////////////////////////////////////////////////
// The font and colours page of the settings dialog: the one font, the list
// of kinds of text, and for the selected kind its colours and style, with a
// sample drawn in them. The page edits its own copy of the settings.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>
#include <wx/clrpicker.h>
#include <wx/fontpicker.h>

#include "fontcolorsettings.h"

class FRMCLIENT_API ibPanelFontColorSettings : public wxPanel
{
public:

	explicit ibPanelFontColorSettings(wxWindow* parent);

	void SetSettings(const ibFontColorSettings& settings);
	const ibFontColorSettings& GetSettings() const { return m_settings; }

private:

	ibFontColorSettings::DisplayItem GetSelectedDisplayItem() const;

	// The selected kind's colours and style into the fields, and the sample after them.
	void ShowDisplayItem();
	void UpdatePreview();

	void OnFontChanged(wxFontPickerEvent& event);
	void OnDisplayItemSelected(wxCommandEvent& event);
	// A colour or the bold or italic box of the selected kind.
	void OnDisplayItemChanged(wxCommandEvent& event);
	void OnResetToDefaults(wxCommandEvent& event);

	wxFontPickerCtrl* m_fontPicker = nullptr;
	wxListBox* m_displayItemList = nullptr;
	wxColourPickerCtrl* m_foreColorPicker = nullptr;
	wxColourPickerCtrl* m_backColorPicker = nullptr;
	wxCheckBox* m_boldCheck = nullptr;
	wxCheckBox* m_italicCheck = nullptr;
	wxPanel* m_previewPanel = nullptr;
	wxStaticText* m_previewText = nullptr;

	ibFontColorSettings m_settings;
};

#endif
