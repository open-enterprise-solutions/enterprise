#include "form.h"

#include "backend/backend_picture.h"

static const ibString s_form_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAzUlEQVR4nOzZoQ3CQBhH8UfDEBhWQFEDBiaCUWAiMGCKYgUMW4AiIYS2qbz+38/1ruql36XJVYSrCGcAwhmAcAYg3PR3YbnavBix2/U8+X72C2jbeNZHxmTW7P6uewgSzjOAwjwOi879+f7OEI4A4TwDKMzQGe/jCBDOM6Bt4/Pv3FxOlKxebzv3HQHCGYBwBiCcAQhnAMIZgHAGIJwBCGcAwhmAcAYgnAEIZwDCGYBw3g73vdB3u1o6R4Bw8QEmhHMECGcAwhmAcPEB3gAAAP///j3DhQAAAAZJREFUAwCZdBDuKKLY4gAAAABJRU5ErkJggg==";

ibPicture ibValueForm::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueForm::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_form_png, ibSize(16, 16));
	return picture;
}