////////////////////////////////////////////////////////////////////////////
//	Description : copy and paste — the same two doors the keyboard uses
////////////////////////////////////////////////////////////////////////////
//
// ⭐ NOTHING IS RE-IMPLEMENTED HERE. ibValueMetaObject::CopyObject and
// ::PasteObject already carry everything a copy has to carry, and none of it is
// obvious: a copy-guid generated over the whole subtree and erased on the way
// out; a paste that re-homes a form's source hops onto the NEW object; a paste
// that runs the metaobject's create events with pasteObjectFlag so a copied
// catalog registers its queryable source; a merge BY NAME, so the payload
// carries no class id and pasting a Document's values onto a Constant is a
// legitimate ask. Writing any of that a second time would be writing it wrong.
//
// So these two tools are: find the object, call the method, hold the bytes.
//
// ⭐ WHY THIS IS WORTH A TOOL AT ALL, beyond saving calls. A copy carries the
// properties the caller has no verb for — everything the object knows about
// itself, not the handful a tool can name. "Copy that document and add an
// attribute" is one call plus one edit; building the same thing property by
// property is a list of the properties the caller happens to know, which is a
// different and smaller object that LOOKS right.
//
// And a paste is a round trip through the object's own save/load. When
// something comes back missing, the missing thing is exactly what does not
// serialise — the same defect class as the spreadsheet groups that were stored
// nowhere until the day they were noticed. The verb is a completeness check
// that happens to be useful.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"
#include "backend/mcp/mcpClipboard.h"

#include "backend/fileSystem/fs.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"

namespace {

ibMetaData* OpenConfiguration(wxString& refusal)
{
	ibMetaData* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	return metaData;
}

// The object a call is about — by id, which is how everything else here
// addresses one.
ibValueMetaObject* FindObject(ibMetaData* metaData, const ibDataNode& params,
	const wxString& field, wxString& refusal)
{
	const ibDataValue* asked = params.FindField(field);

	if (asked == nullptr || asked->Kind() != ibDataKind::Number) {
		refusal = wxString::Format(ibMcpText("No %s given."), field);
		return nullptr;
	}

	ibValueMetaObject* found = ibFindMetaObjectById(metaData, (ibMetaID)asked->AsInt());

	if (found == nullptr) {
		refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %s."),
			asked->AsNumber().ToString());
		return nullptr;
	}

	return found;
}

void DescribeSlot(ibDataNode& result, const wxString& name, const ibMcpClipboardSlot& slot)
{
	result.SetValue(wxT("slot"), name);
	result.SetValue(wxT("holds"), ibMcpClipboardKindName(slot.m_kind));
	result.SetValue(wxT("name"), slot.m_name);
	result.SetValue(wxT("what"), slot.m_what);
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The object's NodeId. Anything in the tree may be copied - an object, an "
			  "attribute, a tabular section, a form."), /*required*/ true);
	return s_a;
}

const ibArg& ArgSlot()
{
	static const ibArg s_a(wxT("slot"), ibArg::Kind::Text,
		ibMcpText("Which buffer to put it in. Omit for the usual one; name it to hold several "
			  "things at once."));
	return s_a;
}

const ibArg& ArgParentId()
{
	static const ibArg s_a(wxT("parentId"), ibArg::Kind::Whole,
		ibMcpText("Where it goes. Omit for the top level of the configuration."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("What to call it. Omit and it is named after the original with a number, the "
			  "way a pasted object is named in the designer."));
	return s_a;
}

const ibArg& ArgKind()
{
	static const ibArg s_a(wxT("kind"), ibArg::Kind::Text,
		ibMcpText("Paste it as a DIFFERENT kind - the payload is a set of values merged by "
			  "property name, so pasting a document's values onto a catalog is allowed and "
			  "keeps whatever the two have in common. Omit to paste the kind that was "
			  "copied."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// metadata_copy
//---------------------------------------------------------------------------

class ibMcpToolMetadataCopy : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_copy"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("copying a metadata object");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Copy a metadata object - with everything under it - into the caller's own "
			"buffer, to be pasted somewhere else. The copy carries EVERY property the object "
			"has, including the ones no tool can name, which is why copying and then editing "
			"is a truer way to make a near-duplicate than building one property by property.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgSlot() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* object = FindObject(metaData, params, wxT("id"), refusal);
		if (object == nullptr)
			return false;

		ibWriterMemory writer;

		if (!metaData->CopyMetaObject(object, writer)) {
			refusal = wxString::Format(ibMcpText("'%s' could not be copied."), object->GetName());
			return false;
		}

		const wxString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		slot.m_kind = ibMcpClipboardKind::Metadata;
		slot.m_name = object->GetName();
		slot.m_what = object->GetClassName();
		slot.m_payload = ibDataNode();
		slot.m_payload.SetValue(wxT("bytes"), writer.buffer());

		DescribeSlot(result, slotName.IsEmpty() ? wxString(wxT("default")) : slotName, slot);
		result.AddField(wxT("bytes"), ibDataValue::Int((s64)writer.size()));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataCopy);

//---------------------------------------------------------------------------
// metadata_paste
//---------------------------------------------------------------------------
//
// ⭐ THE CLASS COMES FROM THE COPY, NOT FROM THE PAYLOAD. CopyObject writes no
// class id on purpose (a paste is a merge by name, and the target's class is
// decided by where it lands), so the class has to be supplied by the caller.
// Remembering the SOURCE's class in the slot makes the ordinary case - copy a
// document, paste a document - need no argument at all, while `kind` stays
// available for the deliberate cross-class paste the payload was designed to
// allow.
//
class ibMcpToolMetadataPaste : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_paste"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("pasting a metadata object");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Paste what metadata_copy put in the buffer, as a new object under a parent. "
			"The new object gets a name of its own so it does not collide with the original; "
			"pass one to say what it should be called.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgParentId(), ArgName(), ArgKind() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		const wxString slotName = ArgSlot().Text(params);

		ibMcpClipboardSlot& slot = ibMcpClipboard(slotName);

		if (slot.IsEmpty()) {
			refusal = ibMcpText("Nothing has been copied. Call metadata_copy first.");
			return false;
		}

		// A CONTROL IN THE BUFFER IS NOT A REFUSAL TO WORK AROUND — the two
		// payloads are both chunked memory and would half-parse into each
		// other, leaving an object that looks pasted and is not.
		if (slot.m_kind != ibMcpClipboardKind::Metadata) {
			refusal = wxString::Format(
				ibMcpText("The buffer holds %s, not a metadata object."),
				ibMcpClipboardKindName(slot.m_kind));
			return false;
		}

		// WHICH CLASS. The copy's own by default; the caller's when they say so.
		wxString kind = ArgKind().Text(params);
		if (kind.IsEmpty())
			kind = slot.m_what;

		const ibClassID clsid = ibResolveMetaKind(metaData, kind);
		if (clsid == 0) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a kind of metadata object in this configuration."), kind);
			return false;
		}

		ibValueMetaObject* parent = nullptr;

		if (const ibDataValue* parentId = params.FindField(ArgParentId().Name())) {
			if (parentId->Kind() == ibDataKind::Number) {
				parent = ibFindMetaObjectById(metaData, (ibMetaID)parentId->AsInt());
				if (parent == nullptr) {
					refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %s."),
						parentId->AsNumber().ToString());
					return false;
				}
			}
		}

		if (parent == nullptr)
			parent = metaData->GetCommonMetaObject();

		if (parent == nullptr) {
			refusal = ibMcpText("This configuration has no root to add to.");
			return false;
		}

		// THE GATE, asked of the parent — the same question a drop onto that
		// branch asks, so a paste cannot put a thing where a click could not.
		if (!parent->FilterChild(clsid)) {
			refusal = wxString::Format(
				ibMcpText("A %s cannot be added to '%s'."), kind, parent->GetName());
			return false;
		}

		// ⚠ NAMED LOCAL, NOT THE EXPRESSION. The getter returns the buffer by
		// value and ibReaderMemory borrows what it is handed — reading straight
		// out of the call would leave it pointing at memory freed at the
		// semicolon. fs.h deletes the rvalue overload for exactly this, so the
		// mistake would not compile; the local is the way to write it, not a
		// precaution around it.
		wxMemoryBuffer bytes = slot.m_payload.GetValue<wxMemoryBuffer>(wxT("bytes"));

		ibReaderMemory reader(bytes);

		// ⭐ THE SAME DOOR THE DESIGNER'S OWN PASTE USES — where it goes, and what goes there. The
		// shell made with its decisions already taken, the payload read into it, the result
		// announced, and nothing left behind if the payload was bad: all of it is the paste's.
		ibValueMetaObject* created = metaData->PasteMetaObject(clsid, parent, reader);
		if (created == nullptr) {
			refusal = wxString::Format(ibMcpText("'%s' could not be pasted here."), slot.m_name);
			return false;
		}

		// BuildNewName already gave it a name that does not collide. A name the
		// caller asked for replaces that one, and only then.
		const wxString name = ArgName().Text(params);
		if (!name.IsEmpty())
			metaData->RenameMetaObject(created, name);

		// Neither Modify(true) nor telling the tree is repeated here — CreateMetaObject and
		// RenameMetaObject do both. They used to be the caller's, and this verb is where that cost
		// showed: a pasted object existed in the configuration and was INVISIBLE in the designer's
		// navigator until it was reopened, because the second caller had not copied what the first
		// one remembered to do. mcpTool.h warns about exactly this shape.

		// ⚠ AddField WITH AN EXPLICIT Int, NOT SetValue. The node's SetValue
		// dispatches through ibDataCodec<T>, and there is a specialization for
		// s32 and none for s64 — on purpose, the undefined primary template is
		// the guard. A `SetValue(name, (s64)x)` therefore fails to compile inside
		// dataBuilder.h, far from the line that wrote it. Same trap as a bare
		// string literal, which has no codec either.
		result.AddField(wxT("id"), ibDataValue::Int((s64)created->GetMetaID()));
		result.SetValue(wxT("name"), created->GetName());
		result.SetValue(wxT("kind"), created->GetClassName());
		result.SetValue(wxT("parent"), parent->GetName());
		result.SetValue(wxT("copiedFrom"), slot.m_name);

		ibMcpReportComplaints(result, created);

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataPaste);
