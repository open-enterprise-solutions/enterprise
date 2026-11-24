#include "documentJournal.h"

// The glyph: two documents stacked, the front one a list - a row for each document. Drawn in the document's
// blue, so a journal reads as kin of what it lists.

static const ibString s_documentJournal_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M6 0.8 H12 L15 3.8 V11 Q15 12 14 12 H6 Q5 12 5 11 V1.8 Q5 0.8 6 0.8 Z' fill='#74c0fc'/><path d='M2.5 4 H8.5 L11.5 7 V14 Q11.5 15.2 10.3 15.2 H2.5 Q1.3 15.2 1.3 14 V5.2 Q1.3 4 2.5 4 Z' fill='#1c6fd1'/><path d='M3.4 8.4 H8.6 M3.4 10.6 H8.6 M3.4 12.8 H7' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectDocumentJournal::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectDocumentJournal::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_documentJournal_svg);

	return picture;
}
