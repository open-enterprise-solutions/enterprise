///////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/gridctrl.h
// Purpose:     ibGrid controls
// Author:      Paul Gammans, Roger Gammans
// Modified by:
// Created:     11/04/2001
// Copyright:   (c) The Computer Surgery (paul@compsurg.co.uk)
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GENERIC_GRIDCTRL_H_
#define _WX_GENERIC_GRIDCTRL_H_

#include "gridext.h"

#if wxUSE_GRID

#define wxGRID_VALUE_CHOICEINT    wxT("choiceint")
#define wxGRID_VALUE_DATETIME     wxT("datetime")


// the default renderer for the cells containing string data
class ibGridCellStringRenderer : public ibGridCellRenderer
{
public:
	ibGridCellStringRenderer()
		: ibGridCellRenderer()
	{
	}

	ibGridCellStringRenderer(const ibGridCellStringRenderer& other)
		: ibGridCellRenderer(other)
	{
	}

	// draw the string
	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	// return the string extent
	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellStringRenderer(*this);
	}

protected:
	// calc the string extent for given string/font
	wxSize DoGetBestSize(const ibGridCellAttr& attr,
		wxDC& dc,
		const wxString& text, 
		float scale = 1.0f);

private:

	wxString m_cacheString;
};

// the default renderer for the cells containing numeric (long) data
class ibGridCellNumberRenderer : public ibGridCellStringRenderer
{
public:
	explicit ibGridCellNumberRenderer(long minValue = LONG_MIN,
		long maxValue = LONG_MAX)
		: ibGridCellStringRenderer(),
		m_minValue(minValue),
		m_maxValue(maxValue)
	{
	}

	ibGridCellNumberRenderer(const ibGridCellNumberRenderer& other)
		: ibGridCellStringRenderer(other),
		m_minValue(other.m_minValue),
		m_maxValue(other.m_maxValue)
	{
	}

	// draw the string right aligned
	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	virtual wxSize GetMaxBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc) wxOVERRIDE;

	// Optional parameters for this renderer are "<min>,<max>".
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellNumberRenderer(*this);
	}

protected:
	wxString GetString(const ibGrid& grid, int row, int col);

	long m_minValue,
		m_maxValue;
};

class ibGridCellFloatRenderer : public ibGridCellStringRenderer
{
public:
	ibGridCellFloatRenderer(int width = -1,
		int precision = -1,
		int format = 16 /*wxGRID_FLOAT_FORMAT_DEFAULT*/);

	ibGridCellFloatRenderer(const ibGridCellFloatRenderer& other)
		: ibGridCellStringRenderer(other),
		m_width(other.m_width),
		m_precision(other.m_precision),
		m_style(other.m_style),
		m_format(other.m_format)
	{
	}

	// get/change formatting parameters
	int GetWidth() const { return m_width; }
	void SetWidth(int width) { m_width = width; m_format.clear(); }
	int GetPrecision() const { return m_precision; }
	void SetPrecision(int precision) { m_precision = precision; m_format.clear(); }
	int GetFormat() const { return m_style; }
	void SetFormat(int format) { m_style = format; m_format.clear(); }

	// draw the string right aligned with given width/precision
	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	// parameters string format is "width[,precision[,format]]"
	// with format being one of f|e|g|E|F|G
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellFloatRenderer(*this);
	}

protected:
	wxString GetString(const ibGrid& grid, int row, int col);

private:
	// formatting parameters
	int m_width,
		m_precision;

	int m_style;
	wxString m_format;
};

// renderer for boolean fields
class ibGridCellBoolRenderer : public ibGridCellRenderer
{
public:
	ibGridCellBoolRenderer()
		: ibGridCellRenderer()
	{
	}

	ibGridCellBoolRenderer(const ibGridCellBoolRenderer& other)
		: ibGridCellRenderer(other)
	{
	}

	// draw a check mark or nothing
	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	// return the checkmark size
	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	virtual wxSize GetMaxBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellBoolRenderer(*this);
	}
};


#if wxUSE_DATETIME

#include "wx/datetime.h"

namespace ibGridPrivate { class DateParseParams; }

// renderer for the cells containing dates only, without time component
class ibGridCellDateRenderer : public ibGridCellStringRenderer
{
public:
	explicit ibGridCellDateRenderer(const wxString& outformat = wxString());

	ibGridCellDateRenderer(const ibGridCellDateRenderer& other)
		: ibGridCellStringRenderer(other),
		m_oformat(other.m_oformat),
		m_tz(other.m_tz)
	{
	}

	// draw the string right aligned
	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	virtual wxSize GetMaxBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellDateRenderer(*this);
	}

	// output strptime()-like format string
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

protected:
	wxString GetString(const ibGrid& grid, int row, int col);

	// This is overridden in ibGridCellDateTimeRenderer which uses a separate
	// input format and forbids fallback to ParseDate().
	virtual void
		GetDateParseParams(ibGridPrivate::DateParseParams& params) const;

	wxString m_oformat;
	wxDateTime::TimeZone m_tz;
};

// the default renderer for the cells containing times and dates
class ibGridCellDateTimeRenderer : public ibGridCellDateRenderer
{
public:
	ibGridCellDateTimeRenderer(const wxString& outformat = wxASCII_STR(wxDefaultDateTimeFormat),
		const wxString& informat = wxASCII_STR(wxDefaultDateTimeFormat));

	ibGridCellDateTimeRenderer(const ibGridCellDateTimeRenderer& other)
		: ibGridCellDateRenderer(other),
		m_iformat(other.m_iformat)
	{
	}

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellDateTimeRenderer(*this);
	}

protected:
	virtual void
		GetDateParseParams(ibGridPrivate::DateParseParams& params) const wxOVERRIDE;

	wxString m_iformat;
};

#endif // wxUSE_DATETIME

// Renderer for fields taking one of a limited set of values: this is the same
// as the renderer for strings, except that it can implement GetMaxBestSize().
class ibGridCellChoiceRenderer : public ibGridCellStringRenderer
{
public:
	explicit ibGridCellChoiceRenderer(const wxString& choices = wxString());

	ibGridCellChoiceRenderer(const ibGridCellChoiceRenderer& other);

	virtual wxSize GetMaxBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc) wxOVERRIDE;

	// Parameters string is a comma-separated list of values.
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellChoiceRenderer(*this);
	}

protected:

	wxArrayString m_choices;
};


// renders a number using the corresponding text string
class ibGridCellEnumRenderer : public ibGridCellChoiceRenderer
{
public:
	explicit ibGridCellEnumRenderer(const wxString& choices = wxString())
		: ibGridCellChoiceRenderer(choices)
	{
	}

	ibGridCellEnumRenderer(const ibGridCellEnumRenderer& other)
		: ibGridCellChoiceRenderer(other)
	{
	}

	// draw the string right aligned
	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellEnumRenderer(*this);
	}

protected:
	wxString GetString(const ibGrid& grid, int row, int col);
};


class ibGridCellAutoWrapStringRenderer : public ibGridCellStringRenderer
{
public:
	ibGridCellAutoWrapStringRenderer()
		: ibGridCellStringRenderer()
	{
	}

	ibGridCellAutoWrapStringRenderer(const ibGridCellAutoWrapStringRenderer& other)
		: ibGridCellStringRenderer(other)
	{
	}

	virtual void Draw(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		const wxRect& rect,
		int row, int col,
		bool isSelected) wxOVERRIDE;

	virtual wxSize GetBestSize(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col) wxOVERRIDE;

	virtual int GetBestHeight(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col,
		int width) wxOVERRIDE;

	virtual int GetBestWidth(ibGrid& grid,
		ibGridCellAttr& attr,
		wxDC& dc,
		int row, int col,
		int height) wxOVERRIDE;

	virtual ibGridCellRenderer* Clone() const wxOVERRIDE
	{
		return new ibGridCellAutoWrapStringRenderer(*this);
	}

private:
	wxArrayString GetTextLines(ibGrid& grid,
		wxDC& dc,
		const ibGridCellAttr& attr,
		const wxRect& rect,
		int row, int col);

	// Helper methods of GetTextLines()

	// Break a single logical line of text into several physical lines, all of
	// which are added to the lines array. The lines are broken at maxWidth and
	// the dc is used for measuring text extent only.
	void BreakLine(wxDC& dc,
		const wxString& logicalLine,
		wxCoord maxWidth,
		wxArrayString& lines);

	// Break a word, which is supposed to be wider than maxWidth, into several
	// lines, which are added to lines array and the last, incomplete, of which
	// is returned in line output parameter.
	//
	// Returns the width of the last line.
	wxCoord BreakWord(wxDC& dc,
		const wxString& word,
		wxCoord maxWidth,
		wxArrayString& lines,
		wxString& line);
};

#endif  // wxUSE_GRID
#endif // _WX_GENERIC_GRIDCTRL_H_
