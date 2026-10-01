#include "form.h"

#include "backend/backend_picture.h"

static const wxString s_form_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAzUlEQVR4nOzZoQ3CQBhH8UfDEBhWQFEDBiaCUWAiMGCKYgUMW4AiIYS2qbz+38/1ruql36XJVYSrCGcAwhmAcAYg3PR3YbnavBix2/U8+X72C2jbeNZHxmTW7P6uewgSzjOAwjwOi879+f7OEI4A4TwDKMzQGe/jCBDOM6Bt4/Pv3FxOlKxebzv3HQHCGYBwBiCcAQhnAMIZgHAGIJwBCGcAwhmAcAYgnAEIZwDCGYBw3g73vdB3u1o6R4Bw8QEmhHMECGcAwhmAcPEB3gAAAP///j3DhQAAAAZJREFUAwCZdBDuKKLY4gAAAABJRU5ErkJggg==";

wxIcon ibValueForm::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_form_png, wxSize(16, 16));
}

wxIcon ibValueForm::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_form_png, wxSize(16, 16));
}