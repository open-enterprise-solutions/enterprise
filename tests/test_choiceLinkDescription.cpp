////////////////////////////////////////////////////////////////////////////
//	Description : ibChoiceLinkDescriptionMemory — a field's link by type and its choice parameters, read back.
//
// The thin designer edits both on the client and sends them back to the server as JSON. The JSON reader
// puts a member that is an object into the node's properties, where WriteNode put the path as a field;
// these pin that a description survives that crossing.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "core/description/choiceLinkDescription.h"
#include "core/serialize/jsonProvider.h"

namespace {

// A value as it crosses the wire: put on a node, written as JSON, read back, taken off the node again.
ibDataValue CrossTheWire(const ibDataValue& value)
{
	ibDataNode root;
	root.SetProperty(wxT("Value"), value);
	ibWriterMemory writer;
	ibJsonProvider().Write(root, writer);
	ibMemoryBuffer bytes = writer.buffer();
	ibReaderMemory reader(bytes);
	ibDataNode read;
	ibJsonProvider().Read(reader, read);
	return read.GetProperty(ibT("Value"));
}

} // namespace

TEST(ChoiceLinkDescription, LinkByType_SurvivesTheWire)
{
	ibChoiceTypeLinkDescription link;
	link.m_source.AppendSource(1050);
	link.m_governedType = 12345;

	ibDataValue written;
	ASSERT_TRUE(ibChoiceLinkDescriptionMemory::WriteNode(written, link));

	ibChoiceTypeLinkDescription read;
	ASSERT_TRUE(ibChoiceLinkDescriptionMemory::ReadNode(CrossTheWire(written), read));
	EXPECT_TRUE(read.IsOk());
	EXPECT_EQ(read.m_source.GetLeaf(), (ibSourceId)1050);
	EXPECT_EQ(read.m_governedType, (ibClassID)12345);
	EXPECT_TRUE(read == link);
}

TEST(ChoiceLinkDescription, ChoiceParameters_SurviveTheWire)
{
	ibChoiceParametersDescription params;
	ibChoiceParameterRowDescription row;
	row.m_parameter = 1008;
	row.m_source.AppendSource(1030);
	row.m_onChange = ibChoiceParameterOnChange::Keep;
	params.m_rows.push_back(row);

	ibDataValue written;
	ASSERT_TRUE(ibChoiceLinkDescriptionMemory::WriteNode(written, params));

	ibChoiceParametersDescription read;
	ASSERT_TRUE(ibChoiceLinkDescriptionMemory::ReadNode(CrossTheWire(written), read));
	ASSERT_EQ(read.m_rows.size(), 1u);
	EXPECT_EQ(read.m_rows[0].m_parameter, (ibMetaID)1008);
	EXPECT_EQ(read.m_rows[0].m_source.GetLeaf(), (ibSourceId)1030);
	EXPECT_EQ(read.m_rows[0].m_onChange, ibChoiceParameterOnChange::Keep);
	EXPECT_TRUE(read == params);
}
