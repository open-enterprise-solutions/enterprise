#include "notebook.h"

#include "core/graphics/picture.h"   // ibPicture::FromBase64 — the master read, scaled to 16 once

static const wxString s_notebookPage_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA+0lEQVR4nOzbsQ7BUBjF8T/xEAbvYGFhYbXZvEWfxVvYbFYWllq8g8FbkDQxkFSTG7297Tm/ReIOksN3+kXaPuL6iHMAiHMAiBuUHYyy25Ma3bfjHgnwCCDOASDOV4HvNyazRdH+D+r1/pzYrpfTx9XHI4A4dwANyc9HYpjOlz/P/QsoOxjmWfEa65tqiksQcQ4AcQ4AcQ4Acd4ECbRab0jJYb8jhEcAce4AAoXOXGo8AohzBxAo9h5QV+d4BBDnDiCQ94COcACIa+3/Af/qII8A4twBBPIe0BEOAHEOAHEOAHGVe0DVfXZt5xFAnHwASTy51SSPAOLkA3gBAAD//4VV4JEAAAAGSURBVAMAJ6UhpAkQEXAAAAAASUVORK5CYII=";

ibPicture ibValueNotebookPage::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueNotebookPage::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_notebookPage_png, ibSize(16, 16));
	return picture;
}