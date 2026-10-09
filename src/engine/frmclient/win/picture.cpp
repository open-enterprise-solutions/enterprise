#include "picture.h"

#include <cstring>
#include <map>
#include <mutex>

#include <wx/base64.h>
#include <wx/image.h>
#include <wx/mstream.h>

namespace {

// The pictures the client was sent, by id — kept by the answer that gave them (the window's thread) and by a list's
// read, and drawn by whoever names them.
std::mutex                          s_picturesLock;
std::map<wxString, wxBitmap>&       Pictures()
{
	static std::map<wxString, wxBitmap>* const s_pictures = new std::map<wxString, wxBitmap>();   // never destroyed: a bitmap outliving wx at the process's end is not freed after it
	return *s_pictures;
}

// A backend picture's number as the server writes it — a decimal: a u64 is past what a JSON number keeps.
wxString IdOf(ibPictureID id)
{
	return wxString::Format(wxT("%llu"), static_cast<unsigned long long>(id));
}

} // namespace

void ibProtocolPictureKeep(const wxString& id, const wxBitmap& picture)
{
	std::lock_guard<std::mutex> lock(s_picturesLock);
	Pictures()[id] = picture;
}

void ibProtocolPictureKeep(ibPictureID id, const wxBitmap& picture)
{
	ibProtocolPictureKeep(IdOf(id), picture);
}

wxBitmap ibProtocolPictureById(ibPictureID id)
{
	const wxString key = IdOf(id);
	std::lock_guard<std::mutex> lock(s_picturesLock);
	const auto found = Pictures().find(key);
	return found != Pictures().end() ? found->second : wxBitmap();
}

wxBitmap ibProtocolPicture(const wxString& value)
{
	// By its id, as a frame names a picture the client was given once…
	{
		std::lock_guard<std::mutex> lock(s_picturesLock);
		const auto found = Pictures().find(value);
		if (found != Pictures().end())
			return found->second;
	}
	// …or the picture itself — what a schema's answer, a choice's items and a picture with no id of its own carry.
	const wxImage image = ibProtocolImage(value);
	return image.IsOk() ? wxBitmap(image) : wxBitmap();
}

wxImage ibProtocolImage(const wxString& base64)
{
	if (base64.IsEmpty())
		return wxImage();
	const wxMemoryBuffer png = wxBase64Decode(base64);
	// A PNG or nothing — an id the client was not given reads as no picture, not as a damaged file said to the person.
	static const char s_signature[] = "\x89PNG\r\n\x1a\n";
	if (png.GetDataLen() < sizeof(s_signature) - 1 || std::memcmp(png.GetData(), s_signature, sizeof(s_signature) - 1) != 0)
		return wxImage();
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
	wxMemoryInputStream stream(png.GetData(), png.GetDataLen());
	return wxImage(stream, wxBITMAP_TYPE_PNG);
}
