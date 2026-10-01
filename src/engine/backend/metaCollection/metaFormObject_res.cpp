#include "metaFormObject.h"

/* PNG */
static const wxString s_form_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA40lEQVR4nOzaMQ4BURSF4X/EIiyEhoZWp7MLa7ELnU5LQ8NC7IJKImJMZkYi5vxfN/d1J3Pvey95PcL1CGcAhDMAwhkA4fqvheF4eqPDLqdD8fztH1C2cB2t6ZLBefW27hAknAEQzgAIZwCEMwDCeRcoW3icnc/HPf9sNJl9XLcFCGcAhHMX4MeqpnRddXctW4BwzgAami+WtLHbbt7W6/Zw2xliCxDOGUBDZT3c1rfPBVVsAcIZAOEMgHCeA2jIu0BHGADhvAsQzgAIV7wWfCscxgAIVxDOFiCcARDOAAgXH8AdAAD//z8i55kAAAAGSURBVAMAgDckXZ4t4q8AAAAASUVORK5CYII=");

wxIcon ibValueMetaObjectForm::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectForm::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_form_16_png, wxSize(16, 16));

	return icon;
}

wxIcon ibValueMetaObjectCommonForm::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommonForm::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_form_16_png, wxSize(16, 16));

	return icon;
}