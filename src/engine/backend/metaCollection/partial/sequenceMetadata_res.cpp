#include "sequence.h"

// The glyph: the documents of a key, left to right, and the BORDER standing after the ones that are
// done — with what lies past it drawn as a broken tail, because that is exactly what a sequence
// says. (Drawn here to get the kind into the tree; a nicer one replaces this base64 and nothing
// else.)

/* PNG */
static const wxString s_sequence_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAPElEQVR4nGNgoCeQy9v3P2rpwxPUN6CiZMF/fJigARvWnP2PD9PeCxQbMBoGpIfBtGnTwIbA6OEQBpQAAK4C0ws4/5czAAAAAElFTkSuQmCC");

wxIcon ibValueMetaObjectSequence::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectSequence::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_sequence_16_png, wxSize(16, 16));

	return icon;
}
