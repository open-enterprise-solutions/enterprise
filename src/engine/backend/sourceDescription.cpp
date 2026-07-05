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
static const unsigned int kSourceDescRawMarker = 0xFFFFFFFFu;   // impossible path count -> unambiguous marker
bool ibSourceDescriptionMemory::LoadData(ibReaderMemory& reader, ibSourceDescription& srcDesc)
{
	srcDesc.ClearSource();
	const unsigned int lead = reader.r_u32();
	if (lead != kSourceDescRawMarker) {
		// Legacy guid-tail blob: a metadata-free load can't resolve its tail (that WAS the metadata coupling
		// we dropped), so recover the head only — `lead` was the legacy path count, the raw head follows.
		if (lead > 0)
			srcDesc.AppendSource((ibSourceId)reader.r_u32());
		return true;
	}
	const unsigned int count = reader.r_u32();
	for (unsigned int i = 0; i < count; i++)
		srcDesc.AppendSource((ibSourceId)reader.r_u32());   // raw id — the source explorer resolves it on the walk
	return true;
}

bool ibSourceDescriptionMemory::SaveData(ibWriterMemory& writer, ibSourceDescription& srcDesc)
{
	const std::vector<ibSourceId>& path = srcDesc.GetPath();
	writer.w_u32(kSourceDescRawMarker);          // raw-layout marker (see LoadData)
	writer.w_u32((unsigned int)path.size());
	for (size_t i = 0; i < path.size(); i++)
		writer.w_u32((unsigned int)path[i]);     // every hop raw — resolution is via the source explorer, not metadata
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
