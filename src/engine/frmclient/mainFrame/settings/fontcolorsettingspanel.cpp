#include "fontcolorsettingspanel.h"

ibPanelFontColorSettings::ibPanelFontColorSettings(wxWindow* parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* const page = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* const fontRow = new wxBoxSizer(wxHORIZONTAL);
	fontRow->Add(new wxStaticText(this, wxID_ANY, _("Font:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_fontPicker = new wxFontPickerCtrl(this, wxID_ANY);
	fontRow->Add(m_fontPicker, 1, wxALIGN_CENTER_VERTICAL);
	page->Add(fontRow, 0, wxEXPAND | wxALL, 5);

	wxBoxSizer* const itemRow = new wxBoxSizer(wxHORIZONTAL);
	m_displayItemList = new wxListBox(this, wxID_ANY);
	for (unsigned int i = 0; i < m_settings.GetNumDisplayItems(); ++i)
		m_displayItemList->Append(ibFontColorSettings::GetDisplayItemName(i));
	itemRow->Add(m_displayItemList, 1, wxEXPAND | wxRIGHT, 5);

	wxFlexGridSizer* const look = new wxFlexGridSizer(2, 5, 5);
	look->Add(new wxStaticText(this, wxID_ANY, _("Foreground:")), 0, wxALIGN_CENTER_VERTICAL);
	m_foreColorPicker = new wxColourPickerCtrl(this, wxID_ANY);
	look->Add(m_foreColorPicker, 0, wxALIGN_CENTER_VERTICAL);
	look->Add(new wxStaticText(this, wxID_ANY, _("Background:")), 0, wxALIGN_CENTER_VERTICAL);
	m_backColorPicker = new wxColourPickerCtrl(this, wxID_ANY);
	look->Add(m_backColorPicker, 0, wxALIGN_CENTER_VERTICAL);
	m_boldCheck = new wxCheckBox(this, wxID_ANY, _("Bold"));
	look->Add(m_boldCheck, 0, wxALIGN_CENTER_VERTICAL);
	m_italicCheck = new wxCheckBox(this, wxID_ANY, _("Italic"));
	look->Add(m_italicCheck, 0, wxALIGN_CENTER_VERTICAL);
	itemRow->Add(look, 0);
	page->Add(itemRow, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// The sample sits on a panel of its own, so the background colour fills more than the text's own box.
	m_previewPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 48)), wxBORDER_SIMPLE);
	m_previewText = new wxStaticText(m_previewPanel, wxID_ANY, wxT("AaBbYyZz 0123 (){};"));
	wxBoxSizer* const previewSizer = new wxBoxSizer(wxVERTICAL);
	previewSizer->AddStretchSpacer();
	previewSizer->Add(m_previewText, 0, wxALIGN_CENTER_HORIZONTAL);
	previewSizer->AddStretchSpacer();
	m_previewPanel->SetSizer(previewSizer);
	page->Add(m_previewPanel, 0, wxEXPAND | wxALL, 5);

	wxButton* const resetButton = new wxButton(this, wxID_ANY, _("Reset to defaults"));
	page->Add(resetButton, 0, wxALL, 5);

	SetSizer(page);

	m_fontPicker->Bind(wxEVT_FONTPICKER_CHANGED, &ibPanelFontColorSettings::OnFontChanged, this);
	m_displayItemList->Bind(wxEVT_LISTBOX, &ibPanelFontColorSettings::OnDisplayItemSelected, this);
	m_foreColorPicker->Bind(wxEVT_COLOURPICKER_CHANGED, &ibPanelFontColorSettings::OnDisplayItemChanged, this);
	m_backColorPicker->Bind(wxEVT_COLOURPICKER_CHANGED, &ibPanelFontColorSettings::OnDisplayItemChanged, this);
	m_boldCheck->Bind(wxEVT_CHECKBOX, &ibPanelFontColorSettings::OnDisplayItemChanged, this);
	m_italicCheck->Bind(wxEVT_CHECKBOX, &ibPanelFontColorSettings::OnDisplayItemChanged, this);
	resetButton->Bind(wxEVT_BUTTON, &ibPanelFontColorSettings::OnResetToDefaults, this);

	SetSettings(m_settings);
}

void ibPanelFontColorSettings::SetSettings(const ibFontColorSettings& settings)
{
	m_settings = settings;
	m_fontPicker->SetSelectedFont(m_settings.GetFont());
	if (m_displayItemList->GetSelection() == wxNOT_FOUND)
		m_displayItemList->SetSelection(ibFontColorSettings::DisplayItem_Default);
	ShowDisplayItem();
}

ibFontColorSettings::DisplayItem ibPanelFontColorSettings::GetSelectedDisplayItem() const
{
	const int selection = m_displayItemList->GetSelection();
	return selection != wxNOT_FOUND
		? static_cast<ibFontColorSettings::DisplayItem>(selection) : ibFontColorSettings::DisplayItem_Default;
}

void ibPanelFontColorSettings::ShowDisplayItem()
{
	const ibFontColorSettings::Colors& colors = m_settings.GetColors(GetSelectedDisplayItem());
	m_foreColorPicker->SetColour(colors.foreColor);
	m_backColorPicker->SetColour(colors.backColor);
	m_boldCheck->SetValue(colors.bold);
	m_italicCheck->SetValue(colors.italic);
	UpdatePreview();
}

void ibPanelFontColorSettings::UpdatePreview()
{
	const ibFontColorSettings::DisplayItem item = GetSelectedDisplayItem();
	const ibFontColorSettings::Colors& colors = m_settings.GetColors(item);

	m_previewPanel->SetBackgroundColour(colors.backColor);
	m_previewText->SetBackgroundColour(colors.backColor);
	m_previewText->SetForegroundColour(colors.foreColor);
	m_previewText->SetFont(m_settings.GetFont(item));
	m_previewPanel->Layout();
	m_previewPanel->Refresh();
}

void ibPanelFontColorSettings::OnFontChanged(wxFontPickerEvent& event)
{
	m_settings.SetFont(event.GetFont());
	UpdatePreview();
}

void ibPanelFontColorSettings::OnDisplayItemSelected(wxCommandEvent&)
{
	ShowDisplayItem();
}

void ibPanelFontColorSettings::OnDisplayItemChanged(wxCommandEvent&)
{
	ibFontColorSettings::Colors colors;
	colors.foreColor = m_foreColorPicker->GetColour();
	colors.backColor = m_backColorPicker->GetColour();
	colors.bold = m_boldCheck->GetValue();
	colors.italic = m_italicCheck->GetValue();
	m_settings.SetColors(GetSelectedDisplayItem(), colors);
	UpdatePreview();
}

void ibPanelFontColorSettings::OnResetToDefaults(wxCommandEvent&)
{
	SetSettings(ibFontColorSettings());
}
