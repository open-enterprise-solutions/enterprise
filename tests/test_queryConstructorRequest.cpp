// WHAT THE QUERY CONSTRUCTOR ASKS A DESIGNER TAB — ibProtocol::ibView::ibQueryConstructor, the server's half
// (frmserver/docView/templates/designer/queryConstructorRequest.h). Asked with no configuration open, the answers are
// the desktop model's own for that case: an empty catalogue, no fields, no parameters — never a failure.

#include <gtest/gtest.h>

#include "core/serialize/dataBuilder.h"
#include "core/query/queryParser.h"
#include "core/query/queryRender.h"
#include "frmserver/docView/templates/designer/queryConstructorRequest.h"
#include "protocol/protocol.h"
#include "backend/query/queryableFactory.h"      // ibQueryableSourceDescriptor — a table the configuration offers
#include "backend/query/tempTableQueryable.h"    // ibTempTableQueryable — its columns, from a table in memory
#include "backend/metadataConfiguration.h"       // ibMetaDataConfigurationFile — the open configuration it registers in

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

ibDataNode CheckRequest(const wxString& text, int statement = -1, int branch = -1)
{
	ibDataNode request;
	request.SetValue(qc::Kind, static_cast<s32>(qc::ibRequest::Check));
	request.SetValue(qc::Text, text);
	if (statement >= 0) {
		request.SetValue(qc::Statement, static_cast<s32>(statement));
		request.SetValue(qc::Branch, static_cast<s32>(branch));
	}
	return request;
}

ibValue TableWith(std::initializer_list<const wxChar*> names)
{
	ibValueModelTable* const table = new ibValueModelTable();
	const ibValue held(table);
	for (const wxChar* name : names)
		table->GetColumnCollection()->AddColumn(name, ibTypeDescription(), name);
	return held;
}

// A document the configuration offers — what the engine's lowering resolves the names of a query against.
class Sales : public ibQueryableSourceDescriptor
{
public:
	Sales() : m_rows(TableWith({ wxT("Region"), wxT("Period"), wxT("Amount") })) {}

	wxString GetNamespace() const override { return wxT("Document"); }
	wxString GetName() const override { return wxT("Sales"); }
	const ibBackendQueryable* CreateQueryable(ibValue**, long) override { return &m_rows; }

private:
	ibTempTableQueryable m_rows;
};

// An OPEN configuration — only an open one has a source factory to register into.
struct OpenConfiguration : ibMetaDataConfigurationFile
{
	LoadGuard m_open{ this };
};

std::vector<wxString> Strings(const ibDataValue* listed)
{
	std::vector<wxString> out;
	if (listed != nullptr)
		for (const ibDataValue& item : listed->AsArray())
			out.push_back(item.AsString());
	return out;
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
	EXPECT_EQ(27, static_cast<int>(qc::ibRequest::Check));
	EXPECT_EQ(23, static_cast<int>(ibProtocol::ibView::ibComposer::ibRequest::Source));
}

TEST(QueryConstructorRequest, WithNoConfigurationTheSourcesAreAnsweredEmpty)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(Request(qc::ibRequest::Sources), response, nullptr));
	const ibDataNode* const sources = response.FindChild(qc::Sources);
	ASSERT_NE(nullptr, sources);
	// …and so are the kinds' pictures: no kind, no row.
	const ibDataNode* const kinds = response.FindChild(qc::Kinds);
	ASSERT_NE(nullptr, kinds);
	EXPECT_TRUE(kinds->Children().empty());
}

TEST(QueryConstructorRequest, AKindWithNoConfigurationHasNoPicture)
{
	// The one road both constructors dress their kinds by (Linq's Kinds, the query constructor's Sources).
	EXPECT_TRUE(ibKindPicture(wxT("Catalog"), nullptr).IsEmpty());
	EXPECT_TRUE(ibKindPicture(wxString(), nullptr).IsEmpty());
}

TEST(QueryConstructorRequest, CheckSaysWhyATextDoesNotParse)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(CheckRequest(wxT("SELECT * FROM Catalog.Goods(")), response, nullptr));
	EXPECT_FALSE(response.GetValue<wxString>(qc::Error).IsEmpty());
	EXPECT_EQ(nullptr, response.FindField(qc::Ungrouped));
}

TEST(QueryConstructorRequest, CheckReadsASoundTextSilently)
{
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(
		CheckRequest(wxT("SELECT S.Region, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region")),
		response, &cfg));
	EXPECT_EQ(nullptr, response.FindField(qc::Error));
}

TEST(QueryConstructorRequest, CheckRefusesANameTheConfigurationHasNot)
{
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(
		CheckRequest(wxT("SELECT S.Nowhere FROM Document.Sales AS S")), response, &cfg));
	EXPECT_FALSE(response.GetValue<wxString>(qc::Error).IsEmpty());
}

TEST(QueryConstructorRequest, CheckWithASelectNamedCompletesItsGroupingInTheSameCall)
{
	// The desktop's OnAddGrouping: grouping by one field of several groups the rest. One call answers the verdict on
	// the text, what that select lacks, and the verdict once it is added — the window's verdict line asks nothing more.
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	const wxString text =
		wxT("SELECT S.Region, S.Period, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region");
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(CheckRequest(text, 0), response, &cfg));
	EXPECT_FALSE(response.GetValue<wxString>(qc::Error).IsEmpty()) << "the text itself is incomplete";

	const std::vector<wxString> ungrouped = Strings(response.FindField(qc::Ungrouped));
	ASSERT_EQ(1u, ungrouped.size());
	EXPECT_EQ(wxT("S.Period"), ungrouped.front());

	const ibDataNode* const completed = response.FindChild(qc::Completed);
	ASSERT_NE(nullptr, completed);
	EXPECT_EQ(nullptr, completed->FindField(qc::Error)) << completed->GetValue<wxString>(qc::Error).ToStdString();

	// …and the completed text is the one the window writes after adding them, so its verdict is found by that text.
	ibQueryPackage window = ibQueryParser().ParsePackage(text);
	for (const wxString& missing : ungrouped)
		window.m_statements.front().m_select->m_groupBy.push_back(ibQueryColumnFromPath(missing));
	EXPECT_EQ(ibRenderQueryPackage(window), completed->GetValue<wxString>(qc::Text));
}

TEST(QueryConstructorRequest, CheckOfASoundGroupingHasNothingToComplete)
{
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(CheckRequest(
		wxT("SELECT S.Region, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region"), 0), response, &cfg));
	EXPECT_TRUE(Strings(response.FindField(qc::Ungrouped)).empty());
	EXPECT_EQ(nullptr, response.FindChild(qc::Completed));
}

TEST(QueryConstructorRequest, CheckOfAStatementNobodyHasCompletesNothing)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(CheckRequest(wxT("SELECT 1 AS One"), 5), response, nullptr));
	EXPECT_EQ(nullptr, response.FindField(qc::Ungrouped));
	EXPECT_EQ(nullptr, response.FindChild(qc::Completed));
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
