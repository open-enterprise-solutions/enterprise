#ifndef __ADVPROP_VALUE_PICTURE_H__
#define __ADVPROP_VALUE_PICTURE_H__

#include <wx/bmpbndl.h>
#include <wx/dc.h>
#include <wx/propgrid/propgrid.h>

// ⭐ A VALUE'S PICTURE IS READ OFF THE VALUE WHEN THE GRID DRAWS, not stored in the property.
//
// wxPGProperty::SetValueImage refuses a property that is not on a grid yet ("Cannot set image for
// detached property"), and ours are made — and given their value — before the inspector appends them:
// the type's picture was set from its constructor's SetValue, which asserted in Debug and set no picture
// at all in Release (2026-10-01). The grid asks OnMeasureImage on every paint and OnCustomPaint to draw,
// so a property that answers those two from its own value has the right picture before it is attached,
// when it is, and after every change. These are the two answers, given the way wxPGProperty gives them
// for the bundle it stores.

inline wxSize ibMeasureValuePicture(const wxBitmapBundle& picture, const wxPropertyGrid* grid)
{
	if (!picture.IsOk())
		return wxSize(0, 0);
	const wxBitmap bmp = grid != nullptr ? picture.GetBitmapFor(grid) : picture.GetBitmap(picture.GetDefaultSize());
	double scale = 1.0;
	if (grid != nullptr && bmp.GetHeight() > grid->GetImageSize().GetHeight())
		scale = (double)grid->GetImageSize().GetHeight() / bmp.GetHeight();
	return wxSize(wxRound(scale * bmp.GetWidth()), wxDefaultCoord);
}

inline void ibPaintValuePicture(const wxBitmapBundle& picture, wxDC& dc, const wxRect& rect, wxPGPaintData& paintData)
{
	if (!picture.IsOk())
		return;
	wxBitmap bmp = picture.GetBitmapFor(paintData.m_parent);
	int yOfs = 0;
	if (bmp.GetHeight() <= rect.height)
		yOfs = (rect.height - bmp.GetHeight()) / 2;
	else
		wxBitmap::Rescale(bmp, wxSize(wxRound(bmp.GetWidth() * (double)rect.height / bmp.GetHeight()), rect.height));
	dc.DrawBitmap(bmp, rect.x, rect.y + yOfs);
}

#endif
