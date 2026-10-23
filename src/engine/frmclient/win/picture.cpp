#include "picture.h"

#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include <wx/image.h>
#include <wx/mstream.h>

#include "core/pictureRegistry.h"              // the engine's own pictures, as the core registered them
#include "core/utils/base64.h"                 // ibBase64 — a picture's PNG, read where its text lies
#include "frmclient/mainFrame/mainFrame.h"     // ibFrontendMainFrame::Fetch — the pictures asked of the server

namespace {

// A picture kept, with the id it was given by — a configuration's carries its version (`<guid>#<version>`).
struct ibKeptPicture {
	wxString id;
	wxBitmap bitmap;
};

// The pictures the client has — the engine's, read from the core once, and the server's, given once — by the picture
// the wire names, the guid before its version: one named by another version is asked for anew and takes its place.
std::mutex                              s_picturesLock;
std::map<wxString, ibKeptPicture>&      Pictures()
{
	static std::map<wxString, ibKeptPicture>* const s_pictures = new std::map<wxString, ibKeptPicture>();   // never destroyed: a bitmap outliving wx at the process's end is not freed after it
	return *s_pictures;
}

// THE PICTURE KEPT FOR THAT ID — of its version, not another's; null: not kept. Under the lock.
const wxBitmap* FindKept(const wxString& id)
{
	const auto found = Pictures().find(id.BeforeFirst(wxT('#')));
	return found != Pictures().end() && found->second.id == id ? &found->second.bitmap : nullptr;
}

// …kept for that id, in the place of another version's. Under the lock.
void Keep(const wxString& id, const wxBitmap& bitmap)
{
	Pictures()[id.BeforeFirst(wxT('#'))] = { id, bitmap };
}

// A backend picture's number as the server writes it — a decimal: a std::uint64_t is past what a JSON number keeps.
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

// The picture ids a node names, wherever they are: a field that holds one by its name (Picture, Icon, OwnerPicture…).
void PicturesOf(const ibProtocolNode& node, std::vector<wxString>& ids)
{
	for (const auto& [name, value] : node.Entries()) {
		if (value.IsString()) {
			if (name == "Icon" || name.EndsWith(wxT("Picture")))
				ids.push_back(value.AsString());
		}
		else if (value.IsNode())
			PicturesOf(value, ids);
		else if (value.IsList()) {
			for (const ibProtocolNode& item : node.GetList(name))
				PicturesOf(item, ids);
		}
	}
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
		if (const wxBitmap* const kept = FindKept(value))
			return *kept;
	}
	ibProtocolPictureReceive({ value });
	std::lock_guard<std::mutex> lock(s_picturesLock);
	const wxBitmap* const kept = FindKept(value);
	return kept != nullptr ? *kept : wxBitmap();
}

wxBitmap ibProtocolPictureById(ibPictureID id)
{
	return id != 0 ? ibProtocolPicture(IdOf(id)) : wxBitmap();
}

void ibProtocolPictureReceive(const std::vector<wxString>& ids)
{
	// Only what the client has not got, each once: one kept is not asked for — kept of another version is — the engine's
	// own is read from the core.
	std::set<wxString> asked;
	ibProtocolNode params;
	for (const wxString& id : ids) {
		if (id.IsEmpty() || IsPictureData(id) || asked.count(id) != 0)
			continue;
		{
			std::lock_guard<std::mutex> lock(s_picturesLock);
			if (FindKept(id) != nullptr)
				continue;
		}
		const wxBitmap engine = EnginePicture(id);
		if (engine.IsOk()) {
			std::lock_guard<std::mutex> lock(s_picturesLock);
			Keep(id, engine);
			continue;
		}
		asked.insert(id);
		params.AddItem(ibProtocol::ibPicture::Pictures, id);
	}
	if (asked.empty())
		return;

	// Not answered — no window yet, refused: nothing is kept, and it is asked again the next time it is named.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	if (frame == nullptr || !frame->Fetch(ibProtocol::ibMethod::Picture, params, answer))
		return;
	for (const ibProtocolNode& picture : answer.FindChild(ibProtocol::ibPicture::Pictures).Children()) {
		const wxImage image = ibProtocolImage(picture.GetString(ibProtocol::ibPicture::Picture));
		std::lock_guard<std::mutex> lock(s_picturesLock);
		Keep(picture.GetString(ibProtocol::ibPicture::Id), image.IsOk() ? wxBitmap(image) : wxBitmap());
	}
}

void ibProtocolPictureReceive(const ibProtocolNode& answer)
{
	std::vector<wxString> ids;
	PicturesOf(answer, ids);
	ibProtocolPictureReceive(ids);
}

wxImage ibProtocolImage(const wxString& base64)
{
	if (base64.IsEmpty())
		return wxImage();
	// A PNG or nothing — data that is not one reads as no picture, not as a damaged file said to the person.
	ibMemoryBuffer png;
	static const char s_signature[] = "\x89PNG\r\n\x1a\n";
	if (!ibBase64::Read(base64.wc_str(), base64.length(), png) || png.GetDataLen() < sizeof(s_signature) - 1 || std::memcmp(png.GetData(), s_signature, sizeof(s_signature) - 1) != 0)
		return wxImage();
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
	wxMemoryInputStream stream(png.GetData(), png.GetDataLen());
	return wxImage(stream, wxBITMAP_TYPE_PNG);
}
