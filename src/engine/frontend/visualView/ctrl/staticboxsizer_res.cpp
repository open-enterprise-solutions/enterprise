#include "sizer.h"

#include "backend/backend_picture.h"

static const wxString s_staticBoxSizer_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAz0lEQVR4nOzbsQ3CMBRF0UvEGMwAc1FRMgMlFXPBDOwBfSgsyw4mvHvqVE963z+RMxFuIpwBEG5LZ7vT/cWCntfDho6sAOEMgHAGQLj4AD7O1KXP8dHme4QVIJzvAqUHeu/e31aaaVaAcM4AKv36nlA7s6wA4ZwBNLoc94x0vj1oYQUIZwCEMwDCuQfQqPUcHs0KEM4ACGcAhHMPoJHfA1bOAAhnAIQzAML5PYBwBkC46hmw9jtDc1aAcM6A0gP/fnfYChDO/wUIZwUIFx/AGwAA//9tl+5+AAAABklEQVQDABAjHlEUXZr1AAAAAElFTkSuQmCC";

wxIcon ibValueStaticBoxSizer::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_staticBoxSizer_png, wxSize(16, 16));
}

wxIcon ibValueStaticBoxSizer::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_staticBoxSizer_png, wxSize(16, 16));
}