// WHAT THE QUERY CONSTRUCTOR ASKS A DESIGNER TAB — ibProtocol::ibView::ibQueryConstructor, the server's half
// (frmserver/docView/templates/designer/queryConstructorRequest.h). Asked with no configuration open, the answers are
// the desktop model's own for that case: an empty catalogue, no fields, no parameters — never a failure.

#include <gtest/gtest.h>

#include "core/serialize/dataBuilder.h"
#include "frmserver/docView/templates/designer/queryConstructorRequest.h"
#include "protocol/protocol.h"

namespace {

namespace qc = ibProtocol::ibView::ibQueryConstructor;

ibDataNode Request(qc::ibRequest kind, const wxString& source = wxString())
{
	ibDataNode request;
	request.SetValue(qc::Kind, static_cast<s32>(kind));
	if (!source.IsEmpty())
		request.SetValue(qc::Source, source);
	return request;
}

} // namespace

TEST(QueryConstructorRequest, ARequestThatIsNotOneOfItsIsLeftToTheTab)
{
	ibDataNode request;
	request.SetValue(qc::Kind, static_cast<s32>(ibProtocol::ibView::ibComposer::ibRequest::Expand));
	ibDataNode response;
	EXPECT_FALSE(ibFetchQueryConstructorRequest(request, response, nullptr));
}

TEST(QueryConstructorRequest, TheNumbersFollowTheComposers)
{
	// protocol.h: a new request takes the next number — the composer's end at Source (23).
	EXPECT_EQ(24, static_cast<int>(qc::ibRequest::Sources));
	EXPECT_EQ(25, static_cast<int>(qc::ibRequest::Fields));
	EXPECT_EQ(26, static_cast<int>(qc::ibRequest::SourceParameters));
	EXPECT_EQ(23, static_cast<int>(ibProtocol::ibView::ibComposer::ibRequest::Source));
}

TEST(QueryConstructorRequest, WithNoConfigurationTheSourcesAreAnsweredEmpty)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(Request(qc::ibRequest::Sources), response, nullptr));
	const ibDataNode* const sources = response.FindChild(qc::Sources);
	ASSERT_NE(nullptr, sources);
}

TEST(QueryConstructorRequest, ASourceNobodyHasHasNoFieldsAndNoError)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(Request(qc::ibRequest::Fields, wxT("Catalog.Nobody")), response, nullptr));
	const ibDataNode* const fields = response.FindChild(qc::Fields);
	ASSERT_NE(nullptr, fields);
	EXPECT_TRUE(fields->Children().empty());
	EXPECT_EQ(nullptr, response.FindField(qc::Error));
}

TEST(QueryConstructorRequest, ASourceTheParserRefusesSaysWhy)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(Request(qc::ibRequest::Fields, wxT("Catalog.Goods(")), response, nullptr));
	EXPECT_FALSE(response.GetValue<wxString>(qc::Error).IsEmpty());
}

TEST(QueryConstructorRequest, ASourceNobodyHasTakesNoParameters)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(
		Request(qc::ibRequest::SourceParameters, wxT("AccumulationRegister.Nobody.Balance")), response, nullptr));
	const ibDataNode* const parameters = response.FindChild(qc::Parameters);
	ASSERT_NE(nullptr, parameters);
	EXPECT_TRUE(parameters->Children().empty());
}
