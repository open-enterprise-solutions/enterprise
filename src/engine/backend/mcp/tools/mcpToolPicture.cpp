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

// The metatype's own name, resolved through ibResolveMetaKind like every other kind.
static const wxChar* const kPictureKind = wxT("Picture");

#include <wx/base64.h>
#include <wx/mstream.h>
#include <wx/image.h>

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
		ibMcpText("What to call the image handed over in `data`."));
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
			"handed over as base64 - see picture_set.");
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
			line->AddField(wxT("id"), ibDataValue::Int((s64)entry.m_id));
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

				if (ibValueMetaObject* object =
					ibFindMetaObject(activeMetaData, kPictureKind, name))
					line->AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));

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
			"declares, or `data` with base64 image bytes and a `name` for it. Objects that carry "
			"a picture are the ones that show one - commands and sections; an object with no "
			"picture of its own is refused rather than quietly ignored.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgEngine(), ArgConfiguration(), ArgData() };
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
				ibMcpText("'%s' has no picture of its own - commands and sections do. Nothing was set."),
				object->GetName());
			return false;
		}

		const wxString fromEngine = ArgEngine().Text(params);
		const wxString fromConfig = ArgConfiguration().Text(params);
		const wxString fromData   = ArgData().Text(params);

		const int said = (fromEngine.IsEmpty() ? 0 : 1)
			+ (fromConfig.IsEmpty() ? 0 : 1)
			+ (fromData.IsEmpty()   ? 0 : 1);

		// ONE SOURCE, SAID OUT LOUD. Neither guessed from what is present nor resolved by
		// precedence: a caller that sent two meant one of them, and picking silently picks wrong
		// half the time.
		if (said != 1) {
			refusal = said == 0
				? ibMcpText("Say where the picture comes from: `engine`, `configuration`, or `data` with "
					"base64 bytes. Nothing was set.")
				: ibMcpText("Say only ONE source - `engine`, `configuration` or `data`. Nothing was set.");
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
				refusal = ibMcpText("An image handed over in `data` needs a `name` to be known by. "
					"Nothing was set.");
				return false;
			}

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

			ibExternalPictureDescription external;
			external.m_img_name   = std::string(name.utf8_str());
			external.m_img_buffer = std::string(
				static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
			external.m_width      = (unsigned int)image.GetWidth();
			external.m_height     = (unsigned int)image.GetHeight();

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
		result.AddField(wxT("picture"), described);
		result.AddField(wxT("empty"), ibDataValue::Bool(stored.IsEmptyPicture()));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPictureSet);
