#include "listBoxVisualData.h"

#include <wx/bitmap.h>
#include <wx/mstream.h>
#include <wx/rawbmp.h>
#include <wx/xpmdecod.h>

#if defined(__WXMSW__) || defined(__WXMAC__)
#define wxPy_premultiply(p, a)   ((p) * (a) / 0xff)
#else
#define wxPy_premultiply(p, a)   (p)
#endif

#ifdef wxHAS_RAW_BITMAP
wxBitmap BitmapFromRGBAImage(int width, int height, const unsigned char *pixelsImage)
{
	int x, y;
	wxBitmap bmp(width, height, 32);
	wxAlphaPixelData pixData(bmp);

	wxAlphaPixelData::Iterator p(pixData);
	for (y = 0; y < height; y++) {
		p.MoveTo(pixData, 0, y);
		for (x = 0; x < width; x++) {
			unsigned char red = *pixelsImage++;
			unsigned char green = *pixelsImage++;
			unsigned char blue = *pixelsImage++;
			unsigned char alpha = *pixelsImage++;

			p.Red() = wxPy_premultiply(red, alpha);
			p.Green() = wxPy_premultiply(green, alpha);
			p.Blue() = wxPy_premultiply(blue, alpha);
			p.Alpha() = alpha;
			++p;
		}
	}
	return bmp;
}
#else
wxBitmap BitmapFromRGBAImage(int width, int height, const unsigned char *pixelsImage)
{
	const int totalPixels = width * height;
	wxScopedArray<unsigned char> data(3 * totalPixels);
	wxScopedArray<unsigned char> alpha(totalPixels);
	int curDataLocation = 0, curAlphaLocation = 0, curPixelsImageLocation = 0;

	for (int i = 0; i < totalPixels; ++i)
	{
		data[curDataLocation++] = pixelsImage[curPixelsImageLocation++];
		data[curDataLocation++] = pixelsImage[curPixelsImageLocation++];
		data[curDataLocation++] = pixelsImage[curPixelsImageLocation++];
		alpha[curAlphaLocation++] = pixelsImage[curPixelsImageLocation++];
	}

	wxImage img(width, height, data.get(), alpha.get(), true);
	wxBitmap bmp(img);

	return bmp;
}
#endif

ÑListBoxVisualData::ÑListBoxVisualData(int d) :m_desiredVisibleRows(d),
m_useDefaultBgColour(true),
m_useDefaultTextColour(true),
m_useDefaultHighlightBgColour(true),
m_useDefaultHighlightTextColour(true),
m_hasListCtrlAppearance(true),
m_useDefaultCurrentBgColour(true),
m_useDefaultCurrentTextColour(true),
m_listType(nullptr), m_posStart(nullptr), m_startLen(nullptr)
{
	ComputeColours();
}

ÑListBoxVisualData::~ÑListBoxVisualData()
{
	m_imgList.clear();
}

void ÑListBoxVisualData::SetDesiredVisibleRows(int d)
{
	m_desiredVisibleRows = d;
}

int ÑListBoxVisualData::GetDesiredVisibleRows() const
{
	return m_desiredVisibleRows;
}

void ÑListBoxVisualData::RegisterImage(int type, const wxBitmap& bmp)
{
	if (!bmp.IsOk())
		return;

	ImgList::iterator it = m_imgList.find(type);
	if (it != m_imgList.end())
		m_imgList.erase(it);

	m_imgList[type] = bmp;
}

void ÑListBoxVisualData::RegisterImage(int type, const char *xpm_data)
{
	wxXPMDecoder dec;
	wxImage img;

	// This check is borrowed from src/stc/scintilla/src/XPM.cpp.
	// Test done is two parts to avoid possibility of overstepping the memory
	// if memcmp implemented strangely. Must be 4 bytes at least at destination.
	if ((0 == memcmp(xpm_data, "/* X", 4)) &&
		(0 == memcmp(xpm_data, "/* XPM */", 9)))
	{
		wxMemoryInputStream stream(xpm_data, strlen(xpm_data) + 1);
		img = dec.ReadFile(stream);
	}
	else
		img = dec.ReadData(reinterpret_cast<const char* const*>(xpm_data));

	wxBitmap bmp(img);
	RegisterImage(type, bmp);
}

void ÑListBoxVisualData::RegisterRGBAImage(int type, int width, int height,
	const unsigned char *pixelsImage)
{
	wxBitmap bmp = BitmapFromRGBAImage(width, height, pixelsImage);
	RegisterImage(type, bmp);
}

void ÑListBoxVisualData::ClearRegisteredImages()
{
	m_imgList.clear();
}

const wxBitmap* ÑListBoxVisualData::GetImage(int i) const
{
	ImgList::const_iterator it = m_imgList.find(i);

	if (it != m_imgList.end())
		return &(it->second);
	else
		return nullptr;
}

void ÑListBoxVisualData::ComputeColours()
{
	// wxSYS_COLOUR_BTNSHADOW seems to be the closest match with most themes.
	m_borderColour = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW);

	if (m_useDefaultBgColour)
		m_bgColour = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);

	if (m_useDefaultTextColour)
		m_textColour = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT);

	if (m_hasListCtrlAppearance)
	{
		// If m_highlightBgColour and/or m_currentBgColour are not
		// explicitly set, set them to wxNullColour to indicate that they
		// should be drawn with wxRendererNative.
		if (m_useDefaultHighlightBgColour)
			m_highlightBgColour = wxNullColour;

		if (m_useDefaultCurrentBgColour)
			m_currentBgColour = wxNullColour;

#ifdef __WXMSW__
		if (m_useDefaultHighlightTextColour)
			m_highlightTextColour =
			wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT);
#else
		if (m_useDefaultHighlightTextColour)
			m_highlightTextColour = wxSystemSettings::GetColour(
				wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT);
#endif

		if (m_useDefaultCurrentTextColour)
			m_currentTextColour = wxSystemSettings::GetColour(
				wxSYS_COLOUR_LISTBOXTEXT);
	}
	else
	{
#ifdef __WXOSX_COCOA__
		if (m_useDefaultHighlightBgColour)
			m_highlightBgColour = GetListHighlightColour();
#else
		if (m_useDefaultHighlightBgColour)
			m_highlightBgColour =
			wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT);
#endif

		if (m_useDefaultHighlightTextColour)
			m_highlightTextColour =
			wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT);
	}
}

static void SetColourHelper(bool& isDefault, wxColour& itemColour,
	const wxColour& newColour)
{
	isDefault = !newColour.IsOk();
	itemColour = newColour;
}

void ÑListBoxVisualData::SetColours(const wxColour& bg,
	const wxColour& txt,
	const wxColour& hlbg,
	const wxColour& hltext)
{
	SetColourHelper(m_useDefaultBgColour, m_bgColour, bg);
	SetColourHelper(m_useDefaultTextColour, m_textColour, txt);
	SetColourHelper(m_useDefaultHighlightBgColour, m_highlightBgColour, hlbg);
	SetColourHelper(m_useDefaultHighlightTextColour, m_highlightTextColour,
		hltext);
	ComputeColours();
}

const wxColour& ÑListBoxVisualData::GetBorderColour() const
{
	return m_borderColour;
}

const wxColour& ÑListBoxVisualData::GetBgColour() const
{
	return m_bgColour;
}

const wxColour& ÑListBoxVisualData::GetTextColour() const
{
	return m_textColour;
}

const wxColour& ÑListBoxVisualData::GetHighlightBgColour() const
{
	return m_highlightBgColour;
}

const wxColour& ÑListBoxVisualData::GetHighlightTextColour() const
{
	return m_highlightTextColour;
}

void ÑListBoxVisualData::UseListCtrlStyle(bool useListCtrlStyle,
	const wxColour& curBg,
	const wxColour& curText)
{
	m_hasListCtrlAppearance = useListCtrlStyle;
	SetColourHelper(m_useDefaultCurrentBgColour, m_currentBgColour, curBg);
	SetColourHelper(m_useDefaultCurrentTextColour, m_currentTextColour,
		curText);
	ComputeColours();
}

bool ÑListBoxVisualData::HasListCtrlAppearance() const
{
	return m_hasListCtrlAppearance;
}

const wxColour& ÑListBoxVisualData::GetCurrentBgColour() const
{
	return m_currentBgColour;
}

const wxColour& ÑListBoxVisualData::GetCurrentTextColour() const
{
	return m_currentTextColour;
}

void ÑListBoxVisualData::SetSciListData(int* type, int* pos, int* len)
{
	m_listType = type;
	m_posStart = pos;
	m_startLen = len;
}

int ÑListBoxVisualData::GetListType() const
{
	return (m_listType ? *m_listType : 0);
}

int ÑListBoxVisualData::GetPosStart() const
{
	return (m_posStart ? *m_posStart : 0);
}

int ÑListBoxVisualData::GetStartLen() const
{
	return (m_startLen ? *m_startLen : 0);
}