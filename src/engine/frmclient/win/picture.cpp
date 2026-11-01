#include "picture.h"

#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include <wx/image.h>
#include <wx/mstream.h>

#include "core/pictureRegistry.h"              // the engine's own pictures, as the core registered them
#include "core/serialize/dataBuilder.h"        // ibDataNode — a property's row, the field that names a picture
#include "frmclient/mainFrame/mainFrame.h"     // ibFrontendMainFrame::Fetch — the pictures asked of the server
#include "protocol/protocolNode.h"             // ibProtocolNode — the field that names a picture

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

// A picture's PNG as a bitmap — none: no PNG.
wxBitmap BitmapOf(const ibMemoryBuffer& png)
{
	const wxImage image = ibProtocolImage(png);
	return image.IsOk() ? wxBitmap(image) : wxBitmap();
}

// THE ENGINE'S OWN, by its number — the core's master, four times the size it is shown at (tools/pictures/render.js);
// none: not the engine's.
wxImage EngineMaster(const wxString& id)
{
	unsigned long long number = 0;
	if (!id.ToULongLong(&number))
		return wxImage();
	const ibPicture registered = ibPictureRegistry::GetPicture(static_cast<ibPictureID>(number));
	return registered.IsOk() ? ibProtocolImage(registered.GetData()) : wxImage();
}

// …shown at 16.
wxBitmap EnginePicture(const wxString& id)
{
	const wxImage image = EngineMaster(id);
	return image.IsOk() ? wxBitmap(image.Scale(16, 16, wxIMAGE_QUALITY_HIGH)) : wxBitmap();
}

// An image fitted into a square of `pixels`, its proportions kept; none for none.
wxBitmap FittedInto(const wxImage& image, int pixels)
{
	if (!image.IsOk() || pixels <= 0)
		return wxBitmap();
	const int longest = wxMax(image.GetWidth(), image.GetHeight());
	return wxBitmap(image.Scale(wxMax(1, image.GetWidth() * pixels / longest), wxMax(1, image.GetHeight() * pixels / longest),
		wxIMAGE_QUALITY_HIGH));
}

void PicturesOf(const ibDataNode& node, std::vector<wxString>& ids);

// The picture ids a node names, wherever they are: a field that holds one by its name (Picture, Icon, OwnerPicture…). A
// picture's PNG is no id: it is not asked for. Read where it lies, as ibProtocolPicture reads its field — the wire's
// node walked by its handles made one of each entry at every level, every inspector filled (a Debug profile, 2026-10-26).
void PicturesOf(const ibString& name, const ibDataValue& value, std::vector<wxString>& ids)
{
	if (value.Kind() == ibDataKind::String) {
		if (name == "Icon" || name.EndsWith(wxT("Picture")))
			ids.push_back(value.AsString());
	}
	else if (value.Kind() == ibDataKind::Child) {
		if (const auto sub = value.AsChild())
			PicturesOf(*sub, ids);
	}
	else if (value.Kind() == ibDataKind::Array) {
		for (const ibDataValue& item : value.AsArray())
			PicturesOf(ibString(), item, ids);
	}
}

void PicturesOf(const ibDataNode& node, std::vector<wxString>& ids)
{
	for (const auto& [name, value] : node.Fields())
		PicturesOf(name, value, ids);
	for (const ibDataNode& child : node.Children())
		PicturesOf(child, ids);
}

} // namespace

wxBitmap ibProtocolPicture(const ibProtocolNode& node, const char* name)
{
	// The wire's node is the engine's: its field read where it lies, by the one door below.
	const ibDataNode* const read = node.GetBody();
	return read != nullptr ? ibProtocolPicture(*read, name) : wxBitmap();
}

wxBitmap ibProtocolPicture(const ibDataNode& node, const char* name)
{
	const auto value = node.FindField(name);
	return value ? ibProtocolPicture(*value) : wxBitmap();
}

wxBitmap ibProtocolPicture(const ibProtocolNode& node, const char* name, int pixels)
{
	const ibDataNode* const read = node.GetBody();
	const auto value = read != nullptr ? read->FindField(name) : decltype(read->FindField(name))();
	if (!value)
		return wxBitmap();
	if (value->Kind() == ibDataKind::Binary)
		return FittedInto(ibProtocolImage(value->AsBinary()), pixels);   // a picture sent whole, with no id
	if (value->Kind() != ibDataKind::String)
		return wxBitmap();
	// Scaled each time it is asked, nothing kept: it is asked when the server's state changes, not when the button is
	// drawn, which draws the bitmap it was given. The engine's own from its master, which has the size to spare; any
	// other from what the client keeps of it.
	const wxString id(value->AsString());
	const wxImage master = EngineMaster(id);
	wxBitmap fitted;
	if (master.IsOk()) {
		fitted = FittedInto(master, pixels);
	}
	else {
		const wxBitmap kept = ibProtocolPicture(id);
		if (kept.IsOk())
			fitted = FittedInto(kept.ConvertToImage(), pixels);
	}
	return fitted;
}

wxBitmap ibProtocolPicture(const ibDataValue& value)
{
	if (value.Kind() == ibDataKind::Binary)
		return BitmapOf(value.AsBinary());
	return value.Kind() == ibDataKind::String ? ibProtocolPicture(wxString(value.AsString())) : wxBitmap();
}

wxBitmap ibProtocolPicture(const wxString& id)
{
	if (id.IsEmpty())
		return wxBitmap();
	{
		std::lock_guard<std::mutex> lock(s_picturesLock);
		if (const wxBitmap* const kept = FindKept(id))
			return *kept;
	}
	ibProtocolPictureReceive({ id });
	std::lock_guard<std::mutex> lock(s_picturesLock);
	const wxBitmap* const kept = FindKept(id);
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
		if (id.IsEmpty() || asked.contains(id))
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
		const wxBitmap bitmap = BitmapOf(picture.GetBinary(ibProtocol::ibPicture::Picture));
		std::lock_guard<std::mutex> lock(s_picturesLock);
		Keep(picture.GetString(ibProtocol::ibPicture::Id), bitmap);
	}
}

void ibProtocolPictureReceive(const ibProtocolNode& answer)
{
	std::vector<wxString> ids;
	if (const ibDataNode* const read = answer.GetBody())
		PicturesOf(*read, ids);
	ibProtocolPictureReceive(ids);
}

wxImage ibProtocolImage(const ibMemoryBuffer& png)
{
	// A PNG or nothing — data that is not one reads as no picture, not as a damaged file said to the person.
	static const char s_signature[] = "\x89PNG\r\n\x1a\n";
	if (png.GetDataLen() < sizeof(s_signature) - 1 || std::memcmp(png.GetData(), s_signature, sizeof(s_signature) - 1) != 0)
		return wxImage();
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
	wxMemoryInputStream stream(png.GetData(), png.GetDataLen());
	return wxImage(stream, wxBITMAP_TYPE_PNG);
}
