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
#include "backend/query/queryLowering.h"         // the desktop's CheckNames / UngroupedProjections — the reference
#include "backend/query/queryConstructorModel.h" // the desktop's GetSources — the kinds' reference
#include "backend/query/queryable.h"             // ibSourceMetaDataScope
#include "backend/backend_picture.h"             // ibBackendPicture::SendPicture — a kind's picture, the reference
#include "backend/compiler/typeCtor.h"           // ibCtorAbstractType

#include <set>

namespace {

namespace qc = ibProtocol::ibView::ibQueryConstructor;

ibDataNode Request(qc::ibRequest kind, const wxString& source = wxString())
{
	ibDataNode request;
	request.SetValue(qc::Kind, static_cast<std::int32_t>(kind));
	if (!source.IsEmpty())
		request.SetValue(qc::Source, source);
	return request;
}

ibDataNode CheckRequest(const wxString& text)
{
	ibDataNode request;
	request.SetValue(qc::Kind, static_cast<std::int32_t>(qc::ibRequest::Check));
	request.SetValue(qc::Text, text);
	return request;
}

ibDataNode UngroupedRequest(const wxString& text, std::int32_t statement, std::int32_t unionBranch)
{
	ibDataNode request = CheckRequest(text);
	request.SetValue(qc::Statement, statement);
	request.SetValue(qc::UnionBranch, unionBranch);
	return request;
}

// THE DESKTOP'S ANSWER — the window's own calls, made here inside the configuration's scope: the reference the request's
// answer is compared with, word for word.
wxString DesktopCheckNames(const wxString& text, const ibMetaData& metaData)
{
	const ibSourceMetaDataScope scope(&metaData);
	try {
		ibQueryLowering::CheckNames(ibQueryParser().ParsePackage(text), {});
	}
	catch (const ibCoreException& error) {
		return error.GetErrorDescription();
	}
	return wxString();
}

std::vector<wxString> DesktopUngroupedProjections(const wxString& text, const ibMetaData& metaData)
{
	const ibSourceMetaDataScope scope(&metaData);
	const ibQueryPackage package = ibQueryParser().ParsePackage(text);
	std::vector<wxString> out;
	for (const ibQueryAstExprPtr& missing : ibQueryLowering::UngroupedProjections(*package.SingleSelect(), {}))
		out.push_back(ibRenderQueryExpr(*missing));
	return out;
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
	request.SetValue(qc::Kind, static_cast<std::int32_t>(ibProtocol::ibView::ibComposer::ibRequest::Expand));
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

TEST(QueryConstructorRequest, EachKindIsOneRowWithItsClassPicture)
{
	// The kinds' rows are the sources' first segments, once each, and a kind's picture is its class's (LINQ's Kinds).
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(Request(qc::ibRequest::Sources), response, &cfg));
	std::set<wxString> expected;
	for (const ibQueryConstructorSource& source : ibQueryConstructorModel(&cfg).GetSources())
		expected.insert(source.m_path.front());
	std::vector<wxString> kinds;
	for (const ibDataNode& row : response.FindChild(qc::Kinds)->Children()) {
		kinds.push_back(row.GetValue<wxString>(qc::Name));
		const ibCtorAbstractType* const ctor = cfg.GetAvailableCtor(kinds.back());
		EXPECT_EQ(ctor != nullptr ? wxString(ibBackendPicture::SendPicture(ctor->GetClassType())) : wxString(),
			row.FindField(qc::Picture) != nullptr ? row.GetValue<wxString>(qc::Picture) : wxString()) << kinds.back();
	}
	EXPECT_EQ(expected, std::set<wxString>(kinds.begin(), kinds.end()));
	EXPECT_EQ(expected.size(), kinds.size()) << "a kind is one row";
	EXPECT_NE(expected.end(), expected.find(wxT("Document")));
}

TEST(QueryConstructorRequest, CheckSaysWhyATextDoesNotParse)
{
	const wxString text = wxT("SELECT * FROM Catalog.Goods(");
	wxString parser;
	try { ibQueryParser().ParsePackage(text); }
	catch (const ibCoreException& error) { parser = error.GetErrorDescription(); }
	ASSERT_FALSE(parser.IsEmpty());

	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(CheckRequest(text), response, nullptr));
	EXPECT_EQ(parser, response.GetValue<wxString>(qc::Error));
}

TEST(QueryConstructorRequest, CheckAnswersTheDesktopsCheckNames)
{
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	for (const wxString& text : {
			wxString(wxT("SELECT S.Region, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region")),
			wxString(wxT("SELECT S.Nowhere FROM Document.Sales AS S")),
			wxString(wxT("SELECT S.Region, S.Period, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region")) }) {
		const wxString desktop = DesktopCheckNames(text, cfg);
		ibDataNode response;
		ASSERT_TRUE(ibFetchQueryConstructorRequest(CheckRequest(text), response, &cfg));
		EXPECT_EQ(desktop, response.FindField(qc::Error) != nullptr ? response.GetValue<wxString>(qc::Error) : wxString())
			<< text.ToStdString();
		EXPECT_EQ(nullptr, response.FindField(qc::UngroupedProjections)) << "the verdict is asked alone";
	}
	// The three texts above are a sound one and two refusals, so the comparison is not over silence alone.
	EXPECT_TRUE(DesktopCheckNames(wxT("SELECT S.Region, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region"), cfg).IsEmpty());
	EXPECT_FALSE(DesktopCheckNames(wxT("SELECT S.Nowhere FROM Document.Sales AS S"), cfg).IsEmpty());
}

TEST(QueryConstructorRequest, CheckWithTheSelectNamedAnswersTheDesktopsUngroupedProjections)
{
	Sales sales;
	OpenConfiguration cfg;
	ASSERT_NE(nullptr, cfg.GetSourceFactory());
	cfg.RegisterSource(&sales);

	for (const wxString& text : {
			wxString(wxT("SELECT S.Region, S.Period, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region")),
			wxString(wxT("SELECT S.Region, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region")) }) {
		ibDataNode response;
		ASSERT_TRUE(ibFetchQueryConstructorRequest(UngroupedRequest(text, 0, -1), response, &cfg));
		EXPECT_EQ(DesktopUngroupedProjections(text, cfg), Strings(response.FindField(qc::UngroupedProjections)))
			<< text.ToStdString();
		EXPECT_EQ(nullptr, response.FindField(qc::Error)) << "the grouping is asked alone";
	}
	EXPECT_EQ(std::vector<wxString>{ wxT("S.Period") }, DesktopUngroupedProjections(
		wxT("SELECT S.Region, S.Period, SUM(S.Amount) AS Total FROM Document.Sales AS S GROUP BY S.Region"), cfg));
}

TEST(QueryConstructorRequest, CheckOfAStatementNobodyHasHasNoProjections)
{
	ibDataNode response;
	ASSERT_TRUE(ibFetchQueryConstructorRequest(UngroupedRequest(wxT("SELECT 1 AS One"), 5, -1), response, nullptr));
	EXPECT_TRUE(Strings(response.FindField(qc::UngroupedProjections)).empty());
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
