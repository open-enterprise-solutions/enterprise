////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "frontend/visualView/visualEditorBase.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueWindow, IValueControl)

//***********************************************************************************
//*                                    ValueWindow                                  *
//***********************************************************************************

IValueWindow::IValueWindow() : IValueControl()
{
	PropertyContainer *categoryBase = IObjectBase::CreatePropertyContainer("Window");

	categoryBase->AddProperty("minimum_size", PropertyType::PT_WXSIZE);
	categoryBase->AddProperty("maximum_size", PropertyType::PT_WXSIZE);
	categoryBase->AddProperty("font", PropertyType::PT_WXFONT);
	categoryBase->AddProperty("fg", PropertyType::PT_WXCOLOUR);
	categoryBase->AddProperty("bg", PropertyType::PT_WXCOLOUR);

	categoryBase->AddProperty("tooltip", PropertyType::PT_WXSTRING);
	categoryBase->AddProperty("context_menu", PropertyType::PT_BOOL);
	categoryBase->AddProperty("context_help", PropertyType::PT_WXSTRING);
	categoryBase->AddProperty("enabled", PropertyType::PT_BOOL);
	categoryBase->AddProperty("visible", PropertyType::PT_BOOL);

	m_category->AddCategory(categoryBase);
}

//***********************************************************************************
//*                                  Update                                       *
//***********************************************************************************

void IValueWindow::UpdateWindow(wxWindow* window)
{
	// All of the properties of the wxWindow object are applied in this function
	if (!window)
		return;

	// Minimum size
	if (m_minimum_size != wxDefaultSize)
		window->SetMinSize(m_minimum_size);
	// Maximum size
	if (m_maximum_size != wxDefaultSize)
		window->SetMaxSize(m_maximum_size);
	// Font
	if (m_font.IsOk())
		window->SetFont(m_font);
	// Background
	if (m_bg.IsOk())
		window->SetBackgroundColour(m_bg);
	// Foreground
	if (m_fg.IsOk())
		window->SetForegroundColour(m_fg);
	// Extra Style
	if (m_window_extra_style)
		window->SetExtraStyle(m_window_extra_style);
	// Enabled
	window->Enable(m_enabled);
	// Hidden
	window->Show(m_visible);
	// Tooltip
	window->SetToolTip(m_tooltip);

	//after lay out 
	if (m_minimum_size != wxDefaultSize ||
		m_maximum_size != wxDefaultSize)
		window->Layout();
}

#include "frontend/controls/dynamicBorder.h"
#include "frontend/visualView/controls/form.h"

void IValueWindow::UpdateLabelSize(IDynamicBorder *control)
{
	if (control == NULL)
		return;

	IValueFrame *parentFrame = m_formOwner;
	wxASSERT(parentFrame);
	if (parentFrame->GetClassName() == wxT("sizerItem")) {
		parentFrame = parentFrame->GetParent();
	}

	wxBoxSizer *parentSizer =
		dynamic_cast<wxBoxSizer *>(parentFrame->GetWxObject());

	if (control->AllowCalc()) {
		control->BeforeCalc();
	}

	std::function<void(IDynamicBorder *, IValueFrame *, wxBoxSizer *, int&)> updateSizer = [&updateSizer](IDynamicBorder *control, IValueFrame *child, wxBoxSizer *parentSizer, int &maxX) {
		IValueFrame *childParent = child->GetParent();
		if (child->GetClassName() == wxT("sizerItem")) {
			child = child->GetChild(0);
		}
		wxASSERT(child);
		std::function<void(IDynamicBorder *, IValueFrame *, wxBoxSizer *, int&)> calcSizer = [&calcSizer](IDynamicBorder *control, IValueFrame *child, wxBoxSizer *parentSizer, int &maxX) {
			if (child->GetClassName() == wxT("sizerItem")) {
				child = child->GetChild(0);
			}
			wxASSERT(child);

			wxBoxSizer *sizer =
				dynamic_cast<wxBoxSizer *>(child->GetWxObject());

			IDynamicBorder *childCtrl =
				dynamic_cast<IDynamicBorder *>(child->GetWxObject());

			if (childCtrl) {
				if (childCtrl->AllowCalc()) {
					wxStaticText *childStaticText = childCtrl->GetStaticText();
					wxSize oldSize = childStaticText->GetMinSize();
					childStaticText->SetMinSize(wxDefaultSize);
					wxSize childSize = childStaticText->GetBestSize();
					childStaticText->SetMinSize(oldSize);
					if (childSize.GetX() > maxX) {
						maxX = childSize.GetX();
					}
				}
			}

			if (parentSizer->GetOrientation() == wxOrientation::wxHORIZONTAL) {
				return;
			}

			for (unsigned int idx = 0; idx < child->GetChildCount(); idx++) {
				calcSizer(control, child->GetChild(idx), sizer ? sizer : parentSizer, maxX);
			}
		};

		wxBoxSizer *sizer =
			dynamic_cast<wxBoxSizer *>(child->GetWxObject());

		int currMax = maxX;

		IDynamicBorder *childCtrl =
			dynamic_cast<IDynamicBorder *>(child->GetWxObject());

		if (maxX != wxNOT_FOUND || parentSizer->GetOrientation() == wxOrientation::wxVERTICAL) {
			calcSizer(control, childParent, sizer ? sizer : parentSizer, maxX);
		}

		if (childCtrl && childCtrl->AllowCalc()) {
			wxStaticText *childStaticText = childCtrl->GetStaticText();
			if (maxX != wxNOT_FOUND) {
				wxSize childSize = childStaticText->GetBestSize();
				childStaticText->SetMinSize(wxSize(maxX, childSize.GetY()));
			}
			else {
				childStaticText->SetMinSize(wxDefaultSize);
				childStaticText->SetMinSize(childStaticText->GetBestSize());
			}

			if (parentSizer->GetOrientation() == wxOrientation::wxHORIZONTAL) {
				maxX = wxNOT_FOUND;
			}
		}

		for (unsigned int idx = 0; idx < child->GetChildCount(); idx++) {
			updateSizer(control, child->GetChild(idx), sizer ? sizer : parentSizer, currMax);
		}
	};

	int currMaxX = 0;
	for (unsigned int idx = 0; idx < parentFrame->GetChildCount(); idx++) {
		updateSizer(control, parentFrame->GetChild(idx), parentSizer, currMaxX);
	}

	if (control->AllowCalc()) {
		control->AfterCalc();
	}
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

#include "utils/typeconv.h"

bool IValueWindow::LoadData(CMemoryReader &reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_minimum_size = TypeConv::StringToSize(propValue);
	reader.r_stringZ(propValue);
	m_maximum_size = TypeConv::StringToSize(propValue);
	reader.r_stringZ(propValue);
	m_font = TypeConv::StringToFont(propValue);
	reader.r_stringZ(propValue);
	m_fg = TypeConv::StringToColour(propValue);
	reader.r_stringZ(propValue);
	m_bg = TypeConv::StringToColour(propValue);

	reader.r_stringZ(m_tooltip);
	reader.r_stringZ(m_context_help);

	m_context_menu = reader.r_u8();
	m_enabled = reader.r_u8();
	m_visible = reader.r_u8();

	return IValueControl::LoadData(reader);
}

bool IValueWindow::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(
		TypeConv::SizeToString(m_minimum_size)
	);
	writer.w_stringZ(
		TypeConv::SizeToString(m_maximum_size)
	);
	writer.w_stringZ(
		TypeConv::FontToString(m_font)
	);
	writer.w_stringZ(
		TypeConv::ColourToString(m_fg)
	);
	writer.w_stringZ(
		TypeConv::ColourToString(m_bg)
	);
	writer.w_stringZ(
		m_tooltip
	);
	writer.w_stringZ(
		m_context_help
	);
	writer.w_u8(
		m_context_menu
	);
	writer.w_u8(
		m_enabled
	);
	writer.w_u8(
		m_visible
	);

	return IValueControl::SaveData(writer);
}

//***********************************************************************************
//*                                  Attribute                                      *
//***********************************************************************************

void IValueWindow::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	IValueFrame::SetAttribute(aParams, cVal);
}

//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

void IValueWindow::ReadProperty()
{
	IValueFrame::ReadProperty();

	IObjectBase::SetPropertyValue("minimum_size", m_minimum_size);
	IObjectBase::SetPropertyValue("maximum_size", m_maximum_size);
	IObjectBase::SetPropertyValue("font", m_font);
	IObjectBase::SetPropertyValue("fg", m_fg);
	IObjectBase::SetPropertyValue("bg", m_bg);
	IObjectBase::SetPropertyValue("tooltip", m_tooltip);
	IObjectBase::SetPropertyValue("context_menu", m_context_menu);
	IObjectBase::SetPropertyValue("context_help", m_context_help);
	IObjectBase::SetPropertyValue("enabled", m_enabled);
	IObjectBase::SetPropertyValue("visible", m_visible);

	//if we have sizerItem then call him readpropery 
	IValueFrame *sizerItem = GetParent();
	if (sizerItem &&
		sizerItem->GetClassName() == wxT("sizerItem"))
	{
		sizerItem->ReadProperty();
	}
}

void IValueWindow::SaveProperty()
{
	IValueFrame::SaveProperty();

	IObjectBase::GetPropertyValue("minimum_size", m_minimum_size);
	IObjectBase::GetPropertyValue("maximum_size", m_maximum_size);
	IObjectBase::GetPropertyValue("font", m_font);
	IObjectBase::GetPropertyValue("fg", m_fg);
	IObjectBase::GetPropertyValue("bg", m_bg);
	IObjectBase::GetPropertyValue("tooltip", m_tooltip);
	IObjectBase::GetPropertyValue("context_menu", m_context_menu);
	IObjectBase::GetPropertyValue("context_help", m_context_help);
	IObjectBase::GetPropertyValue("enabled", m_enabled);
	IObjectBase::GetPropertyValue("visible", m_visible);

	//if we have sizerItem then call him savepropery 
	IValueFrame *sizerItem = GetParent();
	if (sizerItem &&
		sizerItem->GetClassName() == wxT("sizerItem"))
	{
		sizerItem->SaveProperty();
	}
}