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
// node form — Binary blob. The byte reader / writer is contained here, not in the property.

bool ibSourceDescriptionMemory::ReadNode(const ibDataValue& value, ibSourceDescription& srcDesc)
{
	const wxMemoryBuffer& data = value.AsBinary();
	if (data.GetDataLen()) {
		ibReaderMemory reader(data);
		return LoadData(reader, srcDesc);
	}
	return true;
}

bool ibSourceDescriptionMemory::WriteNode(ibDataValue& value, const ibSourceDescription& srcDesc)
{
	ibWriterMemory writer;
	if (!SaveData(writer, srcDesc))
		return false;
	value = ibDataValue::Binary(writer.buffer());
	return true;
}
