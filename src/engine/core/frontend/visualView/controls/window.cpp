////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : window object
////////////////////////////////////////////////////////////////////////////

#include "window.h"
#include "form.h"

wxIMPLEMENT_ABSTRACT_CLASS(IValueWindow, IValueControl)

//***********************************************************************************
//*                                    ValueWindow                                  *
//***********************************************************************************

IValueWindow::IValueWindow() : IValueControl()
{
}

//***********************************************************************************
//*                                  Update                                       *
//***********************************************************************************

void IValueWindow::UpdateWindow(wxWindow* window)
{
	// All of the properties of the wxWindow object are applied in this function
	if (window == NULL)
		return;

	// Minimum size
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize)
		window->SetMinSize(m_propertyMinSize->GetValueAsSize());

	// Maximum size
	if (m_propertyMaxSize->GetValueAsSize() != wxDefaultSize)
		window->SetMaxSize(m_propertyMaxSize->GetValueAsSize());

	// Font
	if (m_propertyFont->IsOk())
		window->SetFont(m_propertyFont->GetValueAsFont());

	// Foreground
	if (m_propertyFG->IsOk())
		window->SetForegroundColour(m_propertyFG->GetValueAsColour());

	// Background
	if (m_propertyBG->IsOk())
		window->SetBackgroundColour(m_propertyBG->GetValueAsColour());

	CValueForm* ownerForm = GetOwnerForm();
	wxASSERT(ownerForm);

	// Enabled
	window->Enable(m_propertyEnabled->GetValueAsBoolean() && ownerForm->IsFormEnabled());

	// Hidden
	window->Show(m_propertyVisible->GetValueAsBoolean());

	// Tooltip
	window->SetToolTip(m_propertyTooltip->GetValueAsString());

	//after lay out 
	if (m_propertyMinSize->GetValueAsSize() != wxDefaultSize ||
		m_propertyMaxSize->GetValueAsSize() != wxDefaultSize) {
		window->Layout();
	}
}

#include "frontend/controls/dynamicBorder.h"
#include "frontend/visualView/controls/form.h"

void IValueWindow::UpdateLabelSize(IDynamicBorder* control)
{
	if (control == NULL)
		return;

	IValueFrame* parentFrame = m_formOwner;
	wxASSERT(parentFrame);
	if (parentFrame->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		parentFrame = parentFrame->GetParent();
	}

	wxBoxSizer* parentSizer =
		dynamic_cast<wxBoxSizer*>(parentFrame->GetWxObject());

	if (control->AllowCalc()) {
		control->BeforeCalc();
	}

	std::function<void(IDynamicBorder*, IValueFrame*, wxBoxSizer*, int&)> updateSizer =
		[&updateSizer](IDynamicBorder* control, IValueFrame* child, wxBoxSizer* parentSizer, int& maxX) {
		IValueFrame* childParent = child->GetParent();
		if (child->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			child = child->GetChild(0);
		}
		wxASSERT(child);
		std::function<void(IDynamicBorder*, IValueFrame*, wxBoxSizer*, int&)> calcSizer =
			[&calcSizer](IDynamicBorder* control, IValueFrame* child, wxBoxSizer* parentSizer, int& maxX) {
			if (child->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
				child = child->GetChild(0);
			}
			wxASSERT(child);

			wxBoxSizer* sizer =
				dynamic_cast<wxBoxSizer*>(child->GetWxObject());

			IDynamicBorder* childCtrl =
				dynamic_cast<IDynamicBorder*>(child->GetWxObject());

			if (childCtrl != NULL) {
				if (childCtrl->AllowCalc()) {
					wxStaticText* childStaticText = childCtrl->GetStaticText();
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

		wxBoxSizer* sizer =
			dynamic_cast<wxBoxSizer*>(child->GetWxObject());

		int currMax = maxX;

		IDynamicBorder* childCtrl =
			dynamic_cast<IDynamicBorder*>(child->GetWxObject());

		if (maxX != wxNOT_FOUND ||
			parentSizer->GetOrientation() == wxOrientation::wxVERTICAL) {
			calcSizer(control, childParent, sizer ? sizer : parentSizer, maxX);
		}

		if (childCtrl && childCtrl->AllowCalc()) {
			wxStaticText* childStaticText = childCtrl->GetStaticText();
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

bool IValueWindow::LoadData(CMemoryReader& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyMinSize->SetValue(TypeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);
	m_propertyMaxSize->SetValue(TypeConv::StringToSize(propValue));
	reader.r_stringZ(propValue);
	m_propertyFont->SetValue(TypeConv::StringToFont(propValue));
	reader.r_stringZ(propValue);
	m_propertyFG->SetValue(TypeConv::StringToColour(propValue));
	reader.r_stringZ(propValue);
	m_propertyBG->SetValue(TypeConv::StringToColour(propValue));

	reader.r_stringZ(propValue);
	m_propertyTooltip->SetValue(propValue);

	m_propertyEnabled->SetValue(reader.r_u8());
	m_propertyVisible->SetValue(reader.r_u8());

	return IValueControl::LoadData(reader);
}

bool IValueWindow::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(
		m_propertyMinSize->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyMaxSize->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyFont->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyFG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyBG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyTooltip->GetValueAsString()
	);
	writer.w_u8(
		m_propertyEnabled->GetValueAsBoolean()
	);
	writer.w_u8(
		m_propertyVisible->GetValueAsBoolean()
	);

	return IValueControl::SaveData(writer);
}