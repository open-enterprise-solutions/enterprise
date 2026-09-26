#include "notebook.h"

#include "backend/backend_picture.h"

static const wxString s_notebookPage_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA+0lEQVR4nOzbsQ7BUBjF8T/xEAbvYGFhYbXZvEWfxVvYbFYWllq8g8FbkDQxkFSTG7297Tm/ReIOksN3+kXaPuL6iHMAiHMAiBuUHYyy25Ma3bfjHgnwCCDOASDOV4HvNyazRdH+D+r1/pzYrpfTx9XHI4A4dwANyc9HYpjOlz/P/QsoOxjmWfEa65tqiksQcQ4AcQ4AcQ4Acd4ECbRab0jJYb8jhEcAce4AAoXOXGo8AohzBxAo9h5QV+d4BBDnDiCQ94COcACIa+3/Af/qII8A4twBBPIe0BEOAHEOAHEOAHGVe0DVfXZt5xFAnHwASTy51SSPAOLkA3gBAAD//4VV4JEAAAAGSURBVAMAJ6UhpAkQEXAAAAAASUVORK5CYII=";

wxIcon ibValueNotebookPage::GetIcon() const
{
	return ibBackendPicture::GetIconFromBase64(s_notebookPage_png, wxSize(16, 16));
}

wxIcon ibValueNotebookPage::GetIconGroup()
{
	return ibBackendPicture::GetIconFromBase64(s_notebookPage_png, wxSize(16, 16));
}