#include "picture.h"

#include <wx/image.h>
#include <wx/mstream.h>

#include "core/utils/base64.h"   // ibBase64 — a PNG written as text

static void EnsurePngHandler()
{
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
}

wxBitmap ToBitmap(const ibPicture& picture)
{
	const ibMemoryBuffer& png = picture.GetData();
	if (png.IsEmpty())
		return wxNullBitmap;
	EnsurePngHandler();
	return wxBitmap::NewFromPNGData(png.GetData(), png.GetDataLen());
}

wxIcon ToIcon(const ibPicture& picture)
{
	wxIcon icon;
	const wxBitmap bitmap = ToBitmap(picture);
	if (bitmap.IsOk())
		icon.CopyFromBitmap(bitmap);
	return icon;
}

ibString CreateBase64Image(const wxImage& image)
{
	ibString text;
	if (!image.IsOk())
		return text;
	EnsurePngHandler();
	wxMemoryOutputStream stream;
	if (image.SaveFile(stream, wxBITMAP_TYPE_PNG))
		ibBase64::Append(text, stream.GetOutputStreamBuffer()->GetBufferStart(), stream.GetLength());
	return text;
}
