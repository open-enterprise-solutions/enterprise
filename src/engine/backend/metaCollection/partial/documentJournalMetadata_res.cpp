#include "documentJournal.h"

// The glyph: two documents stacked, the front one a list - a document's marker and a row for each. Drawn on
// the document glyph's palette and stroke, so a journal reads as kin of what it lists.

/* PNG */
static const ibString s_documentJournal_16_png = ibT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAnUlEQVR42u3avQmAMBCA0QziGFZWae3sHMNZnShuEAjk58D3wdWBVxxXJCVJ6th53SXyAAAAYC7A6gAAANAGsB+5RB4AAADMBVgdAAAAYgBsz1sdAADqD7cOAAAALEEADiEAAABYggAcQgAAALAEATiEAAAAYAnm8Z+kAAQ/hAD8HWD4Ly8AwZcggFUAUQ4hAAAAzAWINgAAAJCkbn2gQDphxo+t6AAAAABJRU5ErkJggg==");

ibPicture ibValueMetaObjectDocumentJournal::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDocumentJournal::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromBase64(s_documentJournal_16_png, ibSize(16, 16));

	return picture;
}
