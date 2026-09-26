///////////////////////////////////////////////////////////////////////////////
//	Copyright : Maxim Kornienko / Open Enterprise Solutions
//	Name      : mcpToolPicture.cpp
//	Purpose   : `picture_list` / `picture_set` — the three sources a picture comes from.
//
//	⭐⭐ A PICTURE IS A REFERENCE, NOT BYTES, and that is the whole reason these exist.
//
//	`ibPictureDescription` is a TAGGED value with three kinds (pictureDescription.h): one of the
//	engine's own pictures, one declared in the configuration, or an image carried inline. Setting
//	one is therefore not "assign a string" — there is no string that could mean all three — which
//	is why the generic property road cannot do it and this pair can.
//
//	⚠ AND THE TAG IS ANSWERED, NOT GUESSED. `picture_set` takes the source as its own argument
//	rather than sniffing what the value looks like: a name that happens to match an engine picture
//	AND a configuration one is a real possibility, and choosing silently would make the wrong
//	choice invisibly. The caller says which, or is refused.
///////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/backend_picture.h"                        // ibBackendPicture — engine registry + resolver
#include "backend/pictureDescription.h"                     // ibPictureDescription — the tagged value
#include "backend/propertyManager/property/propertyPicture.h"
#include "backend/metaCollection/metaObject.h"              // ibValueMetaObject::GetGuid
#include "backend/metaCollection/metaIntrospect.h"          // ibListMetaObjectNames / ibFindMetaObject — the shared road
#include "backend/metadataConfiguration.h"                  // activeMetaData — the open configuration
#include "backend/metaCollection/metaPictureObject.h"       // ibValueMetaObjectPicture — a configuration's own picture

// The metatype's own name, resolved through ibResolveMetaKind like every other kind.
static const wxChar* const kPictureKind = wxT("Picture");

#include <wx/base64.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/bmpbndl.h>   // wxBitmapBundle::FromSVG — the SVG reader the platform already carries

#include <unordered_map>
#include <vector>

namespace {

// THE OBJECT'S PICTURE PROPERTY, asked of the properties rather than assumed by name.
//
// A command calls it "Picture" and so does a section, but the question being asked is "which of
// your properties HOLDS a picture" - and the property answers that by being one. Matching on the
// name would break on the first object that spells it differently and would silently match a
// string property that happened to be called Picture.
ibPropertyPicture* PictureProperty(ibPropertyObject* object)
{
	if (object == nullptr)
		return nullptr;

	for (unsigned int idx = 0; idx < object->GetPropertyCount(); idx++) {
		if (ibPropertyPicture* picture = dynamic_cast<ibPropertyPicture*>(object->GetProperty(idx)))
			return picture;
	}

	return nullptr;
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("What to call the image handed over in `data` or drawn from `svg` - it is kept with it as "
			  "its Name."));
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The object to give a picture to."));
	return s_a;
}

const ibArg& ArgEngine()
{
	static const ibArg s_a(wxT("engine"), ibArg::Kind::Text,
		ibMcpText("Name of one of the engine's pictures (picture_list -> engine)."));
	return s_a;
}

const ibArg& ArgConfiguration()
{
	static const ibArg s_a(wxT("configuration"), ibArg::Kind::Text,
		ibMcpText("Name of a picture declared in this configuration (picture_list -> configuration)."));
	return s_a;
}

const ibArg& ArgData()
{
	static const ibArg s_a(wxT("data"), ibArg::Kind::Text,
		ibMcpText("Base64 image bytes, for an image that is not in either list."));
	return s_a;
}

const ibArg& ArgSvg()
{
	static const ibArg s_a(wxT("svg"), ibArg::Kind::Text,
		ibMcpText("SVG source, for an image that is DRAWN: turned into a PNG here - 64 x 64 unless `width` / "
			  "`height` say otherwise, its viewBox scaled to fit. Shapes, paths, fills and strokes are read - "
			  "text and filters are not."));
	return s_a;
}

const ibArg& ArgWidth()
{
	static const ibArg s_a(wxT("width"), ibArg::Kind::Whole,
		ibMcpText("Width of the PNG drawn from `svg`, in pixels. Omit for 64 - the size the platform's own "
			  "pictures are drawn at and scaled down from."));
	return s_a;
}

const ibArg& ArgHeight()
{
	static const ibArg s_a(wxT("height"), ibArg::Kind::Whole,
		ibMcpText("Height of the PNG drawn from `svg`, in pixels. Omit for 64."));
	return s_a;
}

// The size the platform's own pictures are drawn at, and scaled down from wherever they are shown - what a
// drawing is made at when no size is asked for.
constexpr int kDrawnSide = 64;

// The largest side a drawing is made at: a picture is kept inside the configuration, and one that size is
// already an illustration rather than an icon.
constexpr int kLargestSide = 1024;

// THE SIZE ASKED FOR, 64 on whichever side was not said.
bool AskedSide(const ibDataNode& params, wxSize& side, wxString& refusal)
{
	const s64 width  = ArgWidth().Given(params)  ? ArgWidth().Whole(params)  : kDrawnSide;
	const s64 height = ArgHeight().Given(params) ? ArgHeight().Whole(params) : kDrawnSide;

	if (width < 1 || height < 1 || width > kLargestSide || height > kLargestSide) {
		refusal = wxString::Format(
			ibMcpText("A drawing is 1 to %d pixels on a side, and %lld x %lld was asked for. Nothing was drawn."),
			kLargestSide, (long long)width, (long long)height);
		return false;
	}

	side = wxSize((int)width, (int)height);
	return true;
}

// ⭐ AN IMAGE DRAWN AS TEXT, turned into an image of its own. A caller that has nothing but this server has
// no rasterizer, and a PNG is not written by hand; SVG is text, and the platform already carries a reader
// for it (nanosvg, behind wxBitmapBundle::FromSVG). What comes out is an ordinary PNG with its measured size -
// the same external picture `data` makes.
bool DrawSvg(const wxString& svg, const wxSize& side, const wxString& name,
	ibExternalPictureDescription& drawn, wxString& refusal)
{
#ifdef wxHAS_SVG
	const wxScopedCharBuffer utf8 = svg.utf8_str();
	const wxBitmapBundle bundle = wxBitmapBundle::FromSVG(utf8.data(), side);
	const wxBitmap bitmap = bundle.IsOk() ? bundle.GetBitmap(side) : wxNullBitmap;

	// ⚠ TWO ANSWERS IN ONE NO: the reader refuses a text that is not SVG and an SVG with no shape in it alike -
	// and the second is the likelier one, since text and filters are passed over (measured 2026-09-22: an SVG of
	// one <text> was answered "not SVG"). The refusal says both.
	if (!bitmap.IsOk()) {
		refusal = ibMcpText("Nothing could be drawn: either that is not SVG, or nothing in it is a shape the "
			"reader draws - shapes, paths, fills and strokes are read; text and filters are not.");
		return false;
	}

	const wxImage image = bitmap.ConvertToImage();

	// 🛑 NOTHING DRAWN IS A REFUSAL, not a transparent square. The reader passes over what it does not
	// know - text, filters - without a word, and a picture that shows nothing would be taken as set.
	bool visible = !image.HasAlpha();
	if (!visible) {
		const unsigned char* alpha = image.GetAlpha();
		for (int at = 0, count = image.GetWidth() * image.GetHeight(); at < count && !visible; ++at)
			visible = alpha[at] != 0;
	}

	if (!visible) {
		refusal = ibMcpText("The SVG drew nothing - every pixel came out transparent. Shapes, paths, fills and "
			"strokes are read; text and filters are not.");
		return false;
	}

	wxMemoryOutputStream out;
	if (!image.SaveFile(out, wxBITMAP_TYPE_PNG)) {
		refusal = ibMcpText("The SVG was drawn, but could not be kept as a PNG.");
		return false;
	}

	std::string png(out.GetLength(), '\0');
	out.CopyTo(png.data(), png.size());

	drawn.m_img_name   = std::string(name.utf8_str());
	drawn.m_img_buffer = std::move(png);
	drawn.m_width      = (unsigned int)image.GetWidth();
	drawn.m_height     = (unsigned int)image.GetHeight();
	return true;
#else
	wxUnusedVar(svg);
	wxUnusedVar(side);
	wxUnusedVar(name);
	wxUnusedVar(drawn);
	refusal = ibMcpText("This build of the platform cannot read SVG.");
	return false;
#endif
}

// The most rectangles a picture is said in: a flat icon needs a few hundred, and one that needs more is a
// photograph, which SVG made of pixels says no better than its bytes do.
constexpr size_t kMostRects = 4096;

// ⭐ AND BACK THE OTHER WAY: an image said as SVG, so a caller that works in SVG can read a picture - one it
// did not draw, or one it drew and has to change - and draw it again through picture_from_svg. A PNG keeps
// no drawing to recover, so what is said is the pixels: each run of one colour a rectangle, runs standing on
// one another joined, every colour ONE path - which also makes the commonest edit, a recolouring, one
// attribute. Drawn back at the same size it gives the same image.
bool SvgFromImage(wxImage image, wxString& svg, wxString& refusal)
{
	const int width = image.GetWidth(), height = image.GetHeight();

	if (width < 1 || height < 1 || width > kLargestSide || height > kLargestSide) {
		refusal = wxString::Format(
			ibMcpText("The picture is %d x %d pixels, and one is said as SVG only up to %d on a side."),
			width, height, kLargestSide);
		return false;
	}

	if (!image.HasAlpha())
		image.InitAlpha();   // a mask becomes transparency; an image with neither is opaque throughout

	const unsigned char* rgb = image.GetData();
	const unsigned char* alpha = image.GetAlpha();

	// Alpha in the top byte, so a colour and its transparency are one key.
	const auto colourAt = [&](int at) -> wxUint32 {
		return ((wxUint32)alpha[at] << 24) | ((wxUint32)rgb[at * 3] << 16)
			| ((wxUint32)rgb[at * 3 + 1] << 8) | (wxUint32)rgb[at * 3 + 2];
	};

	struct Rect { int x, y, w, h; wxUint32 colour; };
	std::vector<Rect> done, open;   // `open` reach the row above, sorted by x

	for (int y = 0; y < height; ++y) {

		std::vector<Rect> next;
		size_t above = 0;

		for (int x = 0; x < width; ) {

			const wxUint32 colour = colourAt(y * width + x);
			int end = x + 1;
			while (end < width && colourAt(y * width + end) == colour)
				++end;

			if ((colour >> 24) != 0) {   // fully transparent is simply not drawn

				while (above < open.size() && open[above].x < x)
					done.push_back(open[above++]);

				if (above < open.size() && open[above].x == x && open[above].w == end - x
					&& open[above].colour == colour) {
					Rect grown = open[above++];
					grown.h++;
					next.push_back(grown);
				}
				else {
					next.push_back({ x, y, end - x, 1, colour });
				}
			}

			x = end;
		}

		while (above < open.size())
			done.push_back(open[above++]);
		open.swap(next);

		if (done.size() + open.size() > kMostRects) {
			refusal = wxString::Format(
				ibMcpText("The picture is too detailed to be said as SVG - more than %u rectangles. It is a "
					"photograph rather than a drawing."), (unsigned int)kMostRects);
			return false;
		}
	}

	done.insert(done.end(), open.begin(), open.end());

	// ONE PATH PER COLOUR, in the order the colours are first met.
	std::vector<wxUint32> order;
	std::unordered_map<wxUint32, wxString> paths;

	for (const Rect& rect : done) {
		auto found = paths.find(rect.colour);
		if (found == paths.end()) {
			order.push_back(rect.colour);
			found = paths.emplace(rect.colour, wxString()).first;
		}
		found->second << wxString::Format(wxT("M%d %dh%dv%dh-%dz"), rect.x, rect.y, rect.w, rect.h, rect.w);
	}

	svg = wxString::Format(wxT("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
		"viewBox=\"0 0 %d %d\">\n"), width, height, width, height);

	for (wxUint32 colour : order) {
		svg << wxString::Format(wxT("<path fill=\"#%06x\""), (unsigned int)(colour & 0xFFFFFF));

		const unsigned int opacity = colour >> 24;
		if (opacity != 255)   // FromCDouble: a decimal POINT, whatever the locale says
			svg << wxT(" fill-opacity=\"") << wxString::FromCDouble(opacity / 255.0, 3) << wxT("\"");

		svg << wxT(" d=\"") << paths[colour] << wxT("\"/>\n");
	}

	svg << wxT("</svg>");
	return true;
}

const ibArg& ArgPicture()
{
	static const ibArg s_a(wxT("picture"), ibArg::Kind::Node,
		ibMcpText("The picture in its own shape, as metadata_properties, form_control or picture_set give it: "
			  "{Type: 1, ClassId}, {Type: 2, Guid} or {Type: 3, Image: {Name, Buffer, Width, Height}} - or the "
			  "image a configuration's Picture object holds, {Name, Buffer, Width, Height}, as it reads."),
		/*required*/ true);
	return s_a;
}

// AN IMAGE FROM ITS OWN BYTES, at its own size.
bool ImageFromBytes(const std::string& bytes, wxImage& image)
{
	if (bytes.empty())
		return false;

	wxLogNull noComplaintsToTheUser;   // a bad image is OUR refusal, not a dialog
	wxMemoryInputStream stream(bytes.data(), bytes.size());
	return image.LoadFile(stream, wxBITMAP_TYPE_ANY) && image.IsOk();
}

// THE IMAGE A CONFIGURATION'S PICTURE OBJECT HOLDS, asked of its properties like the picture of any object is.
const ibPropertyExternalPicture* HeldImage(const ibPropertyObject* object)
{
	if (object == nullptr)
		return nullptr;

	for (unsigned int idx = 0; idx < object->GetPropertyCount(); idx++) {
		if (const ibPropertyExternalPicture* image = dynamic_cast<const ibPropertyExternalPicture*>(object->GetProperty(idx)))
			return image;
	}

	return nullptr;
}

} // namespace

//---------------------------------------------------------------------------
// picture_list
//---------------------------------------------------------------------------

class ibMcpToolPictureList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("picture_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("listing the pictures");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Every picture that can be used without supplying one: the engine's own set, "
			"which is always there, and the pictures this configuration declares. Names from "
			"here are what `picture_set` takes. A third source needs nothing listed - an image "
			"handed over as base64, or drawn as SVG - see picture_set and picture_from_svg.\n"
			"The same entries spell a picture in its own shape, for `value`: an engine picture's `id` is its "
			"`ClassId` (Type 1) - digits in a string, because sixty-four bits do not survive a JSON number, "
			"and the shape takes them back that way - a configuration picture's `guid` is its `Guid` (Type 2).\n"
			"A configuration picture is a COMMON one - kept once because several things show it - and its "
			"`comment` says what it depicts and stands for; one without a comment says nothing a reader can "
			"choose by. pattern_read `form-layout` has the rule.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// THE ENGINE'S SET. Always present - it does not depend on a configuration being open,
		// which is why it is listed first and unconditionally.
		std::vector<ibDataValue> engine;

		for (const ibBackendPictureEntry& entry : ibBackendPicture::GetArrayPicture()) {
			std::shared_ptr<ibDataNode> line = std::make_shared<ibDataNode>();
			line->SetValue(wxT("name"), entry.m_name);
			// ⚠ THE DIGITS, AS TEXT. An engine picture's id fills sixty-four bits, and a JSON number on the
			// far side is a double: read back, it comes rounded and names no picture (2026-09-22, a `Print`
			// id that arrived as ...600 for ...564). Sent as a string it survives both ways, and the shape
			// takes it back in either form (pictureDescription.cpp).
			line->SetValue(wxT("id"), wxString::Format(wxT("%llu"), (unsigned long long)entry.m_id));
			engine.push_back(ibDataValue::Child(line));
		}

		result.AddField(wxT("engine"), ibDataValue::Array(engine));

		// THE CONFIGURATION'S OWN. Absent rather than empty when nothing is open - the two are
		// different answers, and a caller that cannot tell them apart will read "no pictures
		// declared" off a base that is not loaded.
		if (activeMetaData != nullptr && activeMetaData->IsConfigOpen()) {

			std::vector<ibDataValue> declared;

			for (const wxString& name : ibListMetaObjectNames(activeMetaData, kPictureKind)) {

				std::shared_ptr<ibDataNode> line = std::make_shared<ibDataNode>();
				line->SetValue(wxT("name"), name);

				// ⭐ AND ITS GUID, which is what a picture description holds for one of these (Type 2) - the
				// shape a picture property reads back in, and takes back through `value`, names it by that.
				if (ibValueMetaObject* object =
					ibFindMetaObject(activeMetaData, kPictureKind, name)) {
					line->AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));
					line->SetValue(wxT("guid"), ibGuid(object->GetGuid()));

					// ⭐ AND WHAT IT MEANS. A PNG cannot be read, and a common picture is chosen by the next thing
					// that needs one - its Comment is where it says what it depicts and what it stands for.
					const wxString meaning = object->GetComment();
					if (!meaning.IsEmpty())
						line->SetValue(wxT("comment"), meaning);
				}

				declared.push_back(ibDataValue::Child(line));
			}

			result.AddField(wxT("configuration"), ibDataValue::Array(declared));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPictureList);

//---------------------------------------------------------------------------
// picture_set
//---------------------------------------------------------------------------

class ibMcpToolPictureSet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("picture_set"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("setting the picture of '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Give an object its picture. Say WHERE it comes from: `engine` with a name from "
			"picture_list, `configuration` with the name of a picture this configuration "
			"declares, `data` with base64 image bytes and a `name` for it, or `svg` with a `name` - an image "
			"DRAWN as SVG text, turned into a PNG here (64 x 64 unless `width` / `height` say otherwise) and "
			"kept as an image of its own. When no engine picture fits, draw one: flat, a few colours, in the "
			"manner of the engine's set. Objects that carry "
			"a picture are the ones that show one - commands, command groups and sections; an object with no "
			"picture of its own is refused rather than quietly ignored.\n"
			"The same picture can also be given through `value` of metadata_set or form_set in its OWN shape, "
			"as metadata_properties / form_control read it - that is the road for a control on a form, and "
			"picture_from_svg draws one in that shape.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		// ⭐ `name` IS DECLARED HERE because Call ASKS for it: an image handed over in `data` has to be
		// known by something, and Call refuses without one. It was read through ArgName() and left out of
		// this list, so the server — which refuses any argument a tool did not declare — turned it away
		// before Call ever saw it: the only route that takes an image from the caller could not be used at
		// all ("needs a `name`" / "takes no argument called 'name'").
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgEngine(), ArgConfiguration(), ArgData(),
			ArgSvg(), ArgWidth(), ArgHeight(), ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibPropertyPicture* property = PictureProperty(object);

		// ⚠ NO PICTURE OF ITS OWN IS A REFUSAL. Setting nothing and answering true is how a caller
		// comes to believe an icon was assigned to something that cannot show one.
		if (property == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' has no picture of its own - commands, command groups and sections do. Nothing was set."),
				object->GetName());
			return false;
		}

		const wxString fromEngine = ArgEngine().Text(params);
		const wxString fromConfig = ArgConfiguration().Text(params);
		const wxString fromData   = ArgData().Text(params);
		const wxString fromSvg    = ArgSvg().Text(params);

		const int said = (fromEngine.IsEmpty() ? 0 : 1)
			+ (fromConfig.IsEmpty() ? 0 : 1)
			+ (fromData.IsEmpty()   ? 0 : 1)
			+ (fromSvg.IsEmpty()    ? 0 : 1);

		// ONE SOURCE, SAID OUT LOUD. Neither guessed from what is present nor resolved by
		// precedence: a caller that sent two meant one of them, and picking silently picks wrong
		// half the time.
		if (said != 1) {
			refusal = said == 0
				? ibMcpText("Say where the picture comes from: `engine`, `configuration`, `data` with "
					"base64 bytes, or `svg` drawn as text. Nothing was set.")
				: ibMcpText("Say only ONE source - `engine`, `configuration`, `data` or `svg`. Nothing was set.");
			return false;
		}

		ibPictureDescription description;

		if (!fromEngine.IsEmpty()) {

			bool found = false;
			for (const ibBackendPictureEntry& entry : ibBackendPicture::GetArrayPicture()) {
				if (!entry.m_name.IsSameAs(fromEngine, false))
					continue;
				description = ibPictureDescription(entry.m_id);
				found = true;
				break;
			}

			if (!found) {
				refusal = wxString::Format(
					ibMcpText("The engine has no picture called '%s' - picture_list says what it has."),
					fromEngine);
				return false;
			}
		}
		else if (!fromConfig.IsEmpty()) {

			// No open-configuration test here: the object this verb is about was already found by
			// ibMcpObjectNamed above, which cannot answer with one when nothing is open.
			ibValueMetaObject* declared =
				ibFindMetaObject(activeMetaData, kPictureKind, fromConfig);

			if (declared == nullptr) {
				refusal = wxString::Format(
					ibMcpText("This configuration declares no picture called '%s'."), fromConfig);
				return false;
			}

			// ⭐ BY GUID, which is what the description holds for a configuration picture: a name
			// is what a person renames and a metaID is not what this tag carries.
			description = ibPictureDescription(declared->GetGuid());
		}
		else {
			const wxString name = ArgName().Text(params);

			if (name.IsEmpty()) {
				refusal = ibMcpText("An image handed over in `data` or drawn in `svg` needs a `name` to be "
					"known by. Nothing was set.");
				return false;
			}

			ibExternalPictureDescription external;

			if (!fromSvg.IsEmpty()) {
				wxSize side;
				if (!AskedSide(params, side, refusal) || !DrawSvg(fromSvg, side, name, external, refusal))
					return false;
			}
			else {
				const wxMemoryBuffer bytes = wxBase64Decode(fromData);

				if (bytes.GetDataLen() == 0) {
					refusal = ibMcpText("`data` did not decode as base64, or decoded to nothing. "
						"Nothing was set.");
					return false;
				}

				// ⚠ MEASURED, NOT DECLARED. The description carries width and height, and a picture
				// whose size reads 0 x 0 IS the empty picture (IsEmptyPicture) - so taking those from
				// the caller would let a perfectly good image arrive and register as absent. They come
				// from the image itself, which also proves the bytes are an image at all.
				wxMemoryInputStream stream(bytes.GetData(), bytes.GetDataLen());
				wxImage image;

				{
					wxLogNull noComplaintsToTheUser;   // a bad image is OUR refusal, not a dialog
					if (!image.LoadFile(stream, wxBITMAP_TYPE_ANY) || !image.IsOk()) {
						refusal = ibMcpText("Those bytes are not an image the platform can read. "
							"Nothing was set.");
						return false;
					}
				}

				external.m_img_name   = std::string(name.utf8_str());
				external.m_img_buffer = std::string(
					static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
				external.m_width      = (unsigned int)image.GetWidth();
				external.m_height     = (unsigned int)image.GetHeight();
			}

			description = ibPictureDescription(external);
		}

		property->SetValue(description);
		activeMetaData->Modify(true);

		result.SetValue(wxT("object"), object->GetName());
		result.AddField(wxT("set"), ibDataValue::Bool(true));

		// WHAT IT ENDED UP BEING, read back off the property rather than repeated from the
		// argument: the same read a form does, so a value that did not take says so here.
		// ⭐ SAID BY THE DESCRIPTION ITSELF — ibPictureDescriptionMemory::WriteNode, the one that
		// writes a picture to a file. The two facts spelled here by hand (which of the three
		// sources, and whether it is empty) were a reading of the tag; the description says the tag
		// AND what it points at, which is the half a caller needs to ask for the same picture again.
		const ibPictureDescription& stored = property->GetValueAsPictureDesc();

		ibDataValue described;
		if (!ibPictureDescriptionMemory::WriteNode(described, stored)) {
			refusal = ibMcpText("The picture was set, but could not be read back to confirm it.");
			return false;
		}
		ibMcpPictureIdAsText(described);   // the id crosses this door as digits — see mcpTool.h
		result.AddField(wxT("picture"), described);
		result.AddField(wxT("empty"), ibDataValue::Bool(stored.IsEmptyPicture()));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPictureSet);

//---------------------------------------------------------------------------
// picture_from_svg
//---------------------------------------------------------------------------

// ⭐ DRAWN AS TEXT, ANSWERED IN THE PICTURE'S OWN SHAPE. picture_set puts a drawing on a metaobject in one
// call; everything else that holds a picture - a control on a form above all - takes it through `value`, in
// the shape the property reads back in. This answers with exactly that shape, so the caller works in SVG and
// never handles the PNG: the node goes to metadata_set or form_set as it came.
class ibMcpToolPictureFromSvg : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("picture_from_svg"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("drawing a picture from SVG");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Draw a picture from SVG text: the platform turns it into a PNG (64 x 64 unless `width` / "
			"`height` say otherwise) and answers with the picture IN ITS OWN SHAPE - an image of its own, "
			"{Type: 3, Image: {Name, Buffer, Width, Height}}. Send that `value` as it is to metadata_set or "
			"form_set, for a property that holds a picture - a command's, a group's, a button's. This call "
			"sets nothing itself; for a metaobject, picture_set with `svg` draws and sets in one.\n"
			"Draw in the manner of the engine's set: flat, a few colours, one clear shape, no words. Text and "
			"filters are not read, and a drawing that comes out empty is refused. picture_to_svg is the way "
			"back - any picture said as SVG, to change and draw again.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgSvg(), ArgWidth(), ArgHeight(), ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString svg = ArgSvg().Text(params);
		if (svg.IsEmpty()) {
			refusal = ibMcpText("Send the drawing in `svg`.");
			return false;
		}

		const wxString name = ArgName().Text(params);
		if (name.IsEmpty()) {
			refusal = ibMcpText("A drawing needs a `name` to be known by - it is kept with it as its Name.");
			return false;
		}

		wxSize side;
		ibExternalPictureDescription drawn;
		if (!AskedSide(params, side, refusal) || !DrawSvg(svg, side, name, drawn, refusal))
			return false;

		// SAID BY THE DESCRIPTION ITSELF — the writer a picture property reads back with, so what this answers
		// is what `value` takes, byte for byte.
		ibDataValue shaped;
		if (!ibPictureDescriptionMemory::WriteNode(shaped, ibPictureDescription(drawn))) {
			refusal = ibMcpText("The SVG was drawn, but could not be written in the picture's shape.");
			return false;
		}

		ibMcpPictureIdAsText(shaped);   // …and here too — see mcpTool.h
		result.AddField(wxT("value"), shaped);
		result.SetValue(wxT("use"), ibMcpText("Send `value` as it is to metadata_set or form_set, property Picture."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPictureFromSvg);

//---------------------------------------------------------------------------
// picture_to_svg
//---------------------------------------------------------------------------

// ⭐ THE WAY BACK, so SVG is the one format a caller works in: read a picture in its own shape, get SVG,
// change it, draw it again with picture_from_svg. The shape is read by the reader the property itself uses,
// so what one names here is exactly what it could hold - an engine picture nobody has is refused the same.
class ibMcpToolPictureToSvg : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("picture_to_svg"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("saying a picture as SVG");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The way back from picture_from_svg: a picture, in its own shape as metadata_properties, "
			"form_control or picture_set give it, said as SVG - to read it, or to change it and draw it again "
			"with picture_from_svg at the same width and height. An engine picture (Type 1) or a "
			"configuration's (Type 2) is said too, so one of the set can be the start of a new one.\n"
			"A PNG keeps no drawing, so the SVG is its pixels: every colour ONE path of rectangles - a "
			"recolouring is one `fill` - and drawn back at the same size it gives the same image.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgPicture() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// A node may arrive either way — as a child, or as a value carrying one.
		ibDataValue given;
		if (const ibDataNode* child = params.FindChild(ArgPicture().Name()))
			given = ibDataValue::Child(std::make_shared<ibDataNode>(*child));
		else if (const ibDataValue* field = params.FindField(ArgPicture().Name()))
			given = *field;

		// ⭐ EITHER SHAPE A PICTURE IS READ IN: a picture property's {Type, ...}, or the bare image a
		// configuration's Picture object holds - which has no Type, because it can only ever be an image.
		ibPictureDescription description;
		bool read = given.Kind() == ibDataKind::Child && given.AsChild() != nullptr;

		if (read && given.AsChild()->FindField(wxT("Type")) == nullptr) {
			ibExternalPictureDescription held;
			read = ibExternalPictureDescriptionMemory::ReadNode(given, held);
			description = ibPictureDescription(held);
		}
		else if (read) {
			ibMcpPictureIdAsNumber(given);   // the digits this door sends out come back as digits — mcpTool.h
			read = ibPictureDescriptionMemory::ReadNode(given, description);
		}

		if (!read) {
			refusal = ibMcpText("That is not a picture in its own shape - {Type: 1, ClassId}, {Type: 2, Guid}, "
				"{Type: 3, Image} or a Picture object's {Name, Buffer, Width, Height} - or it names one the "
				"platform does not have.");
			return false;
		}

		if (description.IsEmptyPicture()) {
			refusal = ibMcpText("That is the empty picture - there is nothing to say.");
			return false;
		}

		// EVERY PICTURE AT ITS OWN SIZE. An image of its own from its bytes; a configuration's from the bytes its
		// Picture object keeps; one of the engine's set as the set holds it.
		wxImage image;

		if (description.m_type == ibPictureType::eFromFile) {
			ImageFromBytes(description.m_img_data.m_img_buffer, image);
		}
		else if (description.m_type == ibPictureType::eFromConfiguration) {

			// ⚠ NOT THROUGH CreatePicture, which answers at the size a picture is DRAWN at - 16 x 16 - and a
			// 64 x 64 image came back said as 16 x 16 (measured 2026-09-22).
			const ibValueMetaObjectPicture* declared = activeMetaData != nullptr
				? activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectPicture>(description.m_meta_guid)
				: nullptr;
			const ibPropertyExternalPicture* held = HeldImage(declared);

			if (held == nullptr) {
				refusal = ibMcpText("This configuration declares no picture with that Guid - picture_list gives "
					"the ones it has.");
				return false;
			}

			ImageFromBytes(held->GetValueAsPictureDesc().m_img_buffer, image);
		}
		else {
			const wxBitmap bitmap = ibBackendPicture::GetPicture(description.m_class_identifier);
			if (bitmap.IsOk())
				image = bitmap.ConvertToImage();
		}

		if (!image.IsOk()) {
			refusal = ibMcpText("The picture could not be read as an image.");
			return false;
		}

		wxString svg;
		if (!SvgFromImage(image, svg, refusal))
			return false;

		result.SetValue(wxT("svg"), svg);
		result.AddField(wxT("width"), ibDataValue::Int(image.GetWidth()));
		result.AddField(wxT("height"), ibDataValue::Int(image.GetHeight()));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPictureToSvg);
