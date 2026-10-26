#include "notebook.h"

#include "core/graphics/picture.h"   // ibPicture::FromBase64 — the master read, scaled to 16 once

static const wxString s_notebook_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAzklEQVR4nOzbuw3CQBQF0cGiJEgggWZclJuBBBLoCWI2WaFdG6M7JwM5Guk98G8g3EA4AxBuW36xO5xezOh5v25YEUeAcO6A2gGP24UW++P543PvHdO6UxwBwrkDWNg0TbQYx5GeHAHCGYBwBiCc/wNYWO/f8W/PLcpzB0eAcO4AZtZ6PaFVeT2i5AgQzgCEMwDhDEA4AxDOAIQzAOEMQDgDEM4AhDMA4QxAOAMQzgCEMwDhfD6gdkDt/vq/cwQIFx9gVe/w/YIjQLj4AG8AAAD//wOoZcgAAAAGSURBVAMA9oEXby0Wq/0AAAAASUVORK5CYII=";

ibPicture ibValueNotebook::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueNotebook::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_notebook_png, ibSize(16, 16));
	return picture;
}