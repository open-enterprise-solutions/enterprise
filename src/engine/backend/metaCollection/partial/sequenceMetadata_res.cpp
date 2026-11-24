#include "sequence.h"

// The register's square, slate, with the documents of a key on one line, one after another: the order a
// sequence keeps.

static const ibString s_sequence_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='2.5' fill='#5a7b95'/><path d='M4.3 11.7 L11.7 4.3' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='round' stroke-linejoin='round'/><circle cx='4.3' cy='11.7' r='1.7' fill='#ffffff'/><circle cx='8' cy='8' r='1.7' fill='#ffffff'/><circle cx='11.7' cy='4.3' r='1.7' fill='#ffffff'/></svg>");

ibPicture ibValueMetaObjectSequence::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSequence::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_sequence_svg);

	return picture;
}
