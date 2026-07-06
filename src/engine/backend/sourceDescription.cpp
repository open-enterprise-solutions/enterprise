#include "sourceDescription.h"
#include "backend/fileSystem/fs.h"                // ibReaderMemory / ibWriterMemory
#include "backend/serialize/dataBuilder.h"        // ibDataValue — node form (Binary blob)

////////////////////////////////////////////////////////////////////////

// The path serialises as RAW ids — the head is a FORM-LOCAL attribute id, each deeper hop a source-column
// id. Resolution is METADATA-AGNOSTIC: the source explorer WALKS the path (FindById per hop; each hop's
// value yields the next explorer), so it never cares what — if any — metadata backs a hop. The serializer
// therefore stores the ids VERBATIM: no guid, no metaData lookup (the id -> guid coupling belonged to the
// old metadata-dependent model and is gone). A leading sentinel marks this raw layout; a blob WITHOUT it is
// a legacy guid-tail form from the old writer — a metadata-free load recovers only its head (re-save rewrites
// it raw), the deprecated guid tail is skipped, never resolved back through metadata.
static const unsigned int kSourceDescRawMarker   = 0xFFFFFFFFu;   // v1: impossible path count -> raw id-only layout
static const unsigned int kSourceDescRawMarkerV2 = 0xFFFFFFFEu;   // v2: raw {id, expected type} per hop — the correspondence
bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc)
{
	srcDesc.ClearSource();
	const unsigned int lead = reader.r_u32();
	if (lead == kSourceDescRawMarkerV2) {
		// v2: each hop is {id, expected type}. The type rides ALONG so a composite-reference hop keeps the
		// branch the picker pinned — the walk coerces to it instead of guessing.
		const unsigned int count = reader.r_u32();
		for (unsigned int i = 0; i < count; i++) {
			const ibSourceId id = (ibSourceId)reader.r_u32();
			const ibClassID type = (ibClassID)reader.r_u64();
			srcDesc.AppendSource(id, type);
		}
		return true;
	}
	if (lead == kSourceDescRawMarker) {
		// v1: raw id-only (type defaults to undefined = accept as is). Forward-compatible: an old blob loads,
		// its hops just carry no pinned type — the walk falls back to the legacy branch guess.
		const unsigned int count = reader.r_u32();
		for (unsigned int i = 0; i < count; i++)
			srcDesc.AppendSource((ibSourceId)reader.r_u32());
		return true;
	}
	// Legacy guid-tail blob: a metadata-free load can't resolve its tail (that WAS the metadata coupling we
	// dropped), so recover the head only — `lead` was the legacy path count, the raw head follows.
	if (lead > 0)
		srcDesc.AppendSource((ibSourceId)reader.r_u32());
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, ibSourceDescription& srcDesc)
{
	const std::vector<ibSourceHop>& path = srcDesc.GetPath();
	writer.w_u32(kSourceDescRawMarkerV2);         // v2 raw-layout marker (see LoadData)
	writer.w_u32((unsigned int)path.size());
	for (const ibSourceHop& hop : path) {
		writer.w_u32((unsigned int)hop.m_id);     // hop id — resolution is via the source explorer, not metadata
		writer.w_u64((unsigned wxLongLong_t)hop.m_type);   // expected type (undefined where the hop imposes none)
	}
	return true;
}

////////////////////////////////////////////////////////////////////////
// node form — Binary blob (raw id path). The byte reader / writer is contained here, not in the property.

bool ibSourceDescriptionMemory::ReadNode(const ibDataValue& value, ibSourceDescription& srcDesc)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen()) {
		ibReaderMemory reader(data);
		return LoadData(reader, srcDesc);
	}
	return true;
}

bool ibSourceDescriptionMemory::WriteNode(ibDataValue& value, ibSourceDescription& srcDesc)
{
	ibWriterMemory writer;
	if (!SaveData(writer, srcDesc))
		return false;
	value = ibDataValue::Binary(writer.buffer());
	return true;
}
