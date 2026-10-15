#include "picture.h"

#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include <wx/base64.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include "core/pictureRegistry.h"              // the engine's own pictures, as the core registered them
#include "frmclient/mainFrame/mainFrame.h"     // ibFrontendMainFrame::Fetch — the pictures asked of the server

namespace {

// The pictures the client has, by the id the wire names them with — the engine's, read from the core once, and the
// server's, given once — and drawn by whoever names them.
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

// A picture's data itself, not its id — a PNG's base64 begins with its signature's.
bool IsPictureData(const wxString& value)
{
	return value.StartsWith(wxT("iVBORw0KGgo"));
}

// THE ENGINE'S OWN, by its number — the core's master, shown at 16 (tools/pictures/render.js); none: not the engine's.
wxBitmap EnginePicture(const wxString& id)
{
	unsigned long long number = 0;
	if (!id.ToULongLong(&number))
		return wxBitmap();
	const ibPicture registered = ibPictureRegistry::GetPicture(static_cast<ibPictureID>(number));
	const wxImage image = registered.IsOk() ? ibProtocolImage(registered.GetData()) : wxImage();
	return image.IsOk() ? wxBitmap(image.Scale(16, 16, wxIMAGE_QUALITY_HIGH)) : wxBitmap();
}

} // namespace

wxBitmap ibProtocolPicture(const wxString& value)
{
	if (value.IsEmpty())
		return wxBitmap();
	// A picture with no id of its own is its data — a file's, an art of the client's own (the splash's logo).
	if (IsPictureData(value)) {
		const wxImage image = ibProtocolImage(value);
		return image.IsOk() ? wxBitmap(image) : wxBitmap();
	}
	{
		std::lock_guard<std::mutex> lock(s_picturesLock);
		const auto found = Pictures().find(value);
		if (found != Pictures().end())
			return found->second;
	}
	ibProtocolPictureReceive({ value });
	std::lock_guard<std::mutex> lock(s_picturesLock);
	const auto found = Pictures().find(value);
	return found != Pictures().end() ? found->second : wxBitmap();
}

wxBitmap ibProtocolPictureById(ibPictureID id)
{
	return id != 0 ? ibProtocolPicture(IdOf(id)) : wxBitmap();
}

void ibProtocolPictureReceive(const std::vector<wxString>& ids)
{
	// Only what the client has not got, each once: one kept is not asked for, the engine's own is read from the core.
	std::set<wxString> asked;
	ibProtocolNode params;
	for (const wxString& id : ids) {
		if (id.IsEmpty() || IsPictureData(id) || asked.count(id) != 0)
			continue;
		{
			std::lock_guard<std::mutex> lock(s_picturesLock);
			if (Pictures().count(id) != 0)
				continue;
		}
		const wxBitmap engine = EnginePicture(id);
		if (engine.IsOk()) {
			std::lock_guard<std::mutex> lock(s_picturesLock);
			Pictures()[id] = engine;
			continue;
		}
		asked.insert(id);
		params.AddItem(ibProtocolName::Pictures, id);
	}
	if (asked.empty())
		return;

	// Not answered — no window yet, refused: nothing is kept, and it is asked again the next time it is named.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	if (frame == nullptr || !frame->Fetch(ibProtocolMethod::Picture, params, answer))
		return;
	for (const ibProtocolNode& picture : answer.FindChild(ibProtocolName::Pictures).Children()) {
		const wxImage image = ibProtocolImage(picture.GetString(ibProtocolName::Picture));
		std::lock_guard<std::mutex> lock(s_picturesLock);
		Pictures()[picture.GetString(ibProtocolName::Id)] = image.IsOk() ? wxBitmap(image) : wxBitmap();
	}
}

wxImage ibProtocolImage(const wxString& base64)
{
	if (base64.IsEmpty())
		return wxImage();
	const wxMemoryBuffer png = wxBase64Decode(base64);
	// A PNG or nothing — data that is not one reads as no picture, not as a damaged file said to the person.
	static const char s_signature[] = "\x89PNG\r\n\x1a\n";
	if (png.GetDataLen() < sizeof(s_signature) - 1 || std::memcmp(png.GetData(), s_signature, sizeof(s_signature) - 1) != 0)
		return wxImage();
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
	wxMemoryInputStream stream(png.GetData(), png.GetDataLen());
	return wxImage(stream, wxBITMAP_TYPE_PNG);
}
