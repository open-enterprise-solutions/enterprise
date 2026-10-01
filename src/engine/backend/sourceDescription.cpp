#include "sourceDescription.h"
#include "backend/fileSystem/fs.h"                // ibReaderMemory / ibWriterMemory
#include "backend/serialize/dataBuilder.h"        // ibDataValue — node form (Binary blob)

////////////////////////////////////////////////////////////////////////

// A DUMB serializer: it writes / reads the hop path AS-IS, nothing else. No metadata, no copy awareness — the id
// path is stored verbatim ([count : u32][ id : u32, type : u64 ] per hop). What the ids MEAN and how a paste
// transforms them is NOT its business: that contract lives in the OWNER (ibPropertySource::CopyNodeValue /
// PasteNodeValue), which processes the path before writing / after reading.

bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc)
{
	srcDesc.ClearSource();

	// ⚠⚠ THE COUNT IS A CLAIM, NOT A GUARANTEE. It comes out of a stored blob, and a blob can be
	// shorter than it says — written by a build whose hop had no TYPE yet, truncated, or simply not
	// a source description at all. Trusting it walked the reader off the end of the buffer, which in
	// a debug build is an assert nobody can act on ("m_pos + cnt <= m_size") and in a release build
	// is reading whatever memory follows and calling it a class id.
	//
	// Caught opening a saved form: a text control's source property, several levels down
	// ibValueFrame::LoadNode. What a bad blob deserves is an empty description — the control then
	// binds to nothing, which is visible and harmless — not a crash and not invented data.
	if (reader.elapsed() < static_cast<int>(sizeof(u32)))
		return true;   // nothing stored at all

	const unsigned int count = reader.r_u32();

	// ONE HOP = id (u32) + type (u64). Ask before reading each, so a blob that ends early stops here
	// with the hops it really had rather than fabricating the rest.
	const int kHopBytes = static_cast<int>(sizeof(u32) + sizeof(u64));
	for (unsigned int i = 0; i < count; i++) {
		if (reader.elapsed() < kHopBytes)
			break;
		const ibSourceId id = (ibSourceId)reader.r_u32();
		const ibClassID type = (ibClassID)reader.r_u64();   // expected type (undefined where the hop imposes none)
		srcDesc.AppendSource(id, type);
	}
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, const ibSourceDescription& srcDesc)
{
	const std::vector<ibSourceHop>& path = srcDesc.GetPath();
	writer.w_u32((unsigned int)path.size());
	for (const ibSourceHop& hop : path) {
		writer.w_u32((unsigned int)hop.m_id);
		writer.w_u64((unsigned wxLongLong_t)hop.m_type);
	}
	return true;
}

////////////////////////////////////////////////////////////////////////
// node form — A STRUCTURE, not a blob.
//
// ⭐ A BINDING IS A PATH, AND A PATH IS SOMETHING TO READ. It used to travel in
// the node as an opaque Binary block: correct, compact, and unreadable by
// anything except this file. A node exists so that every provider — the binary
// one, the JSON one — renders the same value without knowing what it means, and a
// blob defeats exactly that: the JSON view of a form showed a base64 smear where
// the answer to "what is this control bound to" was sitting.
//
// So the hops go in as what they are: a LIST, one entry per hop, each with the id
// it walks and the type it expects to find there. The type is written only where
// there is one, so a plain path stays a plain list of ids.
//
// ⚠ THE BLOB IS NOT READ BACK, deliberately (Max, 2026-08-30: *"binary will not
// be supported, in sources least of all"*). A fallback branch would keep the old
// shape alive in every file that still had it and in every reader that had to
// know about it — and a format with two shapes is a format that will drift.
// Configurations written before this are re-saved from the designer.
//
// The byte reader / writer below is NOT this: LoadData / SaveData still serve the
// clipboard's own transient form, which rides guids rather than raw ids and never
// reaches a stored file.

namespace {

const wxChar* const kHopsField = wxT("hops");
const wxChar* const kHopId     = wxT("id");
const wxChar* const kHopType   = wxT("type");

} // namespace

bool ibSourceDescriptionMemory::ReadNode(const ibDataValue& value, ibSourceDescription& srcDesc)
{
	const std::shared_ptr<ibDataNode>& node = value.AsChild();
	if (!node)
		return true;   // nothing bound — an ordinary state, not a failure

	const ibDataValue* hops = node->FindField(kHopsField);
	if (hops == nullptr)
		return true;

	for (const ibDataValue& hop : hops->AsArray()) {

		const std::shared_ptr<ibDataNode>& entry = hop.AsChild();
		if (!entry)
			continue;

		const ibSourceId id = (ibSourceId)entry->GetValue<s32>(kHopId);

		// An absent type is the ABSENCE OF A CONSTRAINT, which is why it is not
		// written and why reading it back as undefined is right.
		ibClassID type = g_valueUndefinedCLSID;
		if (const ibDataValue* stored = entry->FindField(kHopType))
			type = (ibClassID)stored->AsUInt();

		srcDesc.AppendSource(id, type);
	}

	return true;
}

bool ibSourceDescriptionMemory::WriteNode(ibDataValue& value, const ibSourceDescription& srcDesc)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	std::vector<ibDataValue> hops;
	for (const ibSourceHop& hop : srcDesc.GetPath()) {

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->AddField(kHopId, ibDataValue::Int((s64)hop.m_id));

		// Only where the hop imposes one — an undefined type is the absence of a
		// constraint, and writing it would put a number where there is no fact.
		if (hop.m_type != g_valueUndefinedCLSID)
			entry->AddField(kHopType, ibDataValue::UInt((u64)hop.m_type));

		hops.push_back(ibDataValue::Child(entry));
	}

	node->AddField(kHopsField, ibDataValue::Array(hops));
	value = ibDataValue::Child(node);
	return true;
}
