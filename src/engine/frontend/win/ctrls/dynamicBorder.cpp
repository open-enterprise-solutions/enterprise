#include "dynamicBorder.h"

wxSize ibDynamicStaticText::DoGetBestClientSize() const
{
	if (m_staticTextCache.IsSameAs(m_labelOrig, m_font)) {
		return m_staticTextCache.m_sizeLabel;
	}

	if (!m_labelOrig.IsEmpty()) {

		// The measuring DC is built here rather than kept as a shared static one. A static
		// wxScreenDC is constructed while the library loads — before the toolkit is up, which
		// GTK does not survive — and it was what forced the critical section around this block,
		// since a shared DC cannot be used from two threads at once. Local, it costs a DC only
		// on a cache miss (a changed label or font), and it needs no locking at all.
		wxScreenDC calcLabelDC;

		calcLabelDC.SetFont(m_font);
		calcLabelDC.GetMultiLineTextExtent(
			m_labelOrig,
			&m_staticTextCache.m_sizeLabel.x, &m_staticTextCache.m_sizeLabel.y
		);

		// This extra pixel is a hack we use to ensure that a wxStaticText
		// vertically centered around the same position as a wxTextCtrl shows its
		// text on exactly the same baseline. It is not clear why is this needed
		// nor even whether this works in all cases, but it does work, at least
		// with the default fonts, under Windows XP, 7 and 8, so just use it for
		// now.
		//
		// In the future we really ought to provide a way for each of the controls
		// to provide information about the position of the baseline for the text
		// it shows and use this information in the sizer code when centering the
		// controls vertically, otherwise we simply can't ensure that the text is
		// always on the same line, e.g. even with this hack wxComboBox text is
		// still not aligned to the same position.
		m_staticTextCache.m_sizeLabel.x++;

		// And this extra pixel is an even worse hack which is somehow needed to
		// avoid the problem with the native control now showing any text at all
		// for some particular width values: e.g. without this, using " AJ" as a
		// label doesn't show anything at all on the screen, even though the
		// control text is properly set and it has roughly the correct (definitely
		// not empty) size. This looks like a bug in the native control because it
		// really should show at least the first characters, but it's not clear
		// what else can we do about it than just add this extra pixel.
		m_staticTextCache.m_sizeLabel.y++;
	}
	else 
	{
		m_staticTextCache.m_sizeLabel.x = 0;
		m_staticTextCache.m_sizeLabel.y = 0;
	}

	m_staticTextCache.m_strLabel = m_labelOrig;
	m_staticTextCache.m_fontLabel = m_font;

	return m_staticTextCache.m_sizeLabel;
}