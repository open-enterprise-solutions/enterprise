#include "metaTableObject.h"

/* PNG */
static const ibString s_table_16_png = ibT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAqElEQVR4nOzZsQ3CMBQA0XPEGLACjAajwGiwAuwBLbKEkEjH3etiJS5O+kXsBbkFuQIgVwDkCoDcZl7YHq9P/tjjchjvz40AcgVArgDIFQC5AiBXAOT6G/z2wv285xe7023VPmu//7TPrBFArgDIFQC5AiBXAOQKgJw+wJgXuhuUKQBynQghVwDkCoBcAZArAHIFQK7zAOQKgNxArhFArgDIFQA5fYAXAAAA///Sc0ISAAAABklEQVQDADcbHoT1eZ8BAAAAAElFTkSuQmCC");

ibPicture ibValueMetaObjectTableData::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectTableData::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_table_16_png, ibSize(16, 16));
	
	return picture;
}
