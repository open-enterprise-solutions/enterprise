#include "metaTableObject.h"

/* PNG */
static const wxString s_table_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAqElEQVR4nOzZsQ3CMBQA0XPEGLACjAajwGiwAuwBLbKEkEjH3etiJS5O+kXsBbkFuQIgVwDkCoDcZl7YHq9P/tjjchjvz40AcgVArgDIFQC5AiBXAOT6G/z2wv285xe7023VPmu//7TPrBFArgDIFQC5AiBXAOQKgJw+wJgXuhuUKQBynQghVwDkCoBcAZArAHIFQK7zAOQKgNxArhFArgDIFQA5fYAXAAAA///Sc0ISAAAABklEQVQDADcbHoT1eZ8BAAAAAElFTkSuQmCC");

wxIcon ibValueMetaObjectTableData::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectTableData::GetIconGroup()
{
	static wxIcon icon = 
		ibBackendPicture::GetIconFromBase64(s_table_16_png, wxSize(16, 16));
	
	return icon;
}
