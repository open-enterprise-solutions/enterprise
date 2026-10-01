#include "notebook.h"

#include "backend/backend_picture.h"

static const wxString s_notebook_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAzklEQVR4nOzbuw3CQBQF0cGiJEgggWZclJuBBBLoCWI2WaFdG6M7JwM5Guk98G8g3EA4AxBuW36xO5xezOh5v25YEUeAcO6A2gGP24UW++P543PvHdO6UxwBwrkDWNg0TbQYx5GeHAHCGYBwBiCc/wNYWO/f8W/PLcpzB0eAcO4AZtZ6PaFVeT2i5AgQzgCEMwDhDEA4AxDOAIQzAOEMQDgDEM4AhDMA4QxAOAMQzgCEMwDhfD6gdkDt/vq/cwQIFx9gVe/w/YIjQLj4AG8AAAD//wOoZcgAAAAGSURBVAMA9oEXby0Wq/0AAAAASUVORK5CYII=";

wxIcon ibValueNotebook::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_notebook_png, wxSize(16, 16));
}

wxIcon ibValueNotebook::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_notebook_png, wxSize(16, 16));
}