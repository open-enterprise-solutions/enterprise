////////////////////////////////////////////////////////////////////////////
//	Description : the start page - which forms it shows, and in how many columns
////////////////////////////////////////////////////////////////////////////
//
// The application root holds ONE ibHomePageDescription (core/description/homePageDescription.h): a template - one
// column or two, and in which ratio - and per column its forms, top to bottom. The studio edits it in a dialog over
// the HomePage schema (frmserver/schema/studio/homePage.cpp); this is the same edit for a caller without a mouse:
// the same forms to choose from (every form the application allows), the same description set on the root, and the
// application marked modified, as the dialog's OK marks it. The words are the description's own - Template, Left,
// Right - and nothing here decides what a form does on the page: the runtime opens it as its own command would.
//
////////////////////////////////////////////////////////////////////////////

#include "frmserver/mcp/mcpTool.h"

#include "backend/metaCollection/metaFormObject.h"       // ibValueMetaObjectFormBase - what the page shows
#include "backend/metaCollection/metaObjectMetadata.h"   // ibValueMetaObjectApplication - whose page it is
#include "backend/metadataApplication.h"

#include <algorithm>
#include <map>

namespace {

// The short spelling every tool file here uses for one declared argument.
using ibArg = ibMcpTool::ibMcpArgument;

// The templates in the order of ibHomePageTemplate, so a word's index IS the value it stands for.
const std::vector<ibString>& TemplateWords()
{
	static const std::vector<ibString> s_words = {
		ibT("oneColumn"), ibT("twoEqualColumns"), ibT("twoColumnsWideLeft"), ibT("twoColumnsWideRight") };
	return s_words;
}

const ibArg& ArgTemplate()
{
	static const ibArg s_a(ibT("template"), ibArg::Kind::Text,
		ibMcpText("How the page is split: `oneColumn`, `twoEqualColumns` (1 : 1), `twoColumnsWideLeft` (2 : 1) or "
			  "`twoColumnsWideRight` (1 : 2). One column shows the right column's forms under the left one's, so "
			  "switching back loses nothing. Omit to leave it."),
		false, TemplateWords());
	return s_a;
}

const ibArg& ArgLeft()
{
	static const ibArg s_a(ibT("left"), ibArg::Kind::Many,
		ibMcpText("The forms of the left column - or of the only one - top to bottom, as NodeIds. They REPLACE what "
			  "the column held; [] empties it. Omit to leave the column as it is."));
	return s_a;
}

const ibArg& ArgRight()
{
	static const ibArg s_a(ibT("right"), ibArg::Kind::Many,
		ibMcpText("The forms of the right column, top to bottom, as NodeIds - the same as `left`."));
	return s_a;
}

// Every form the page can show, by id: a shared form, or a form of any object - the list the studio's picker
// offers (ibSchemaHomePage::Build), the forms the application allows.
std::map<ibMetaID, const ibValueMetaObjectFormBase*> PlaceableForms(const ibMetaDataApplicationBase* metaData)
{
	std::map<ibMetaID, const ibValueMetaObjectFormBase*> forms;
	for (const ibValueMetaObjectFormBase* form :
		metaData->GetAnyArrayObject<ibValueMetaObjectFormBase>({ g_metaSharedFormCLSID, g_metaFormCLSID }, true)) {
		if (form != nullptr && form->IsAllowed())
			forms.emplace(form->GetMetaID(), form);
	}
	return forms;
}

ibDataValue FormEntry(const ibMetaID& id, const std::map<ibMetaID, const ibValueMetaObjectFormBase*>& forms)
{
	std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
	entry->AddField(ibT("id"), ibDataValue::Int((std::int64_t)id));
	const auto found = forms.find(id);
	if (found != forms.end())
		entry->SetValue(ibT("name"), found->second->GetFullName());
	else
		entry->SetValue(ibT("gone"), true);   // kept by the page, shown by nobody: the form is not there any more
	return ibDataValue::Child(entry);
}

// The page as it stands: its template by word and each column's forms with their names.
void AnswerPage(const ibHomePageDescription& page, const std::map<ibMetaID, const ibValueMetaObjectFormBase*>& forms,
	ibDataNode& result)
{
	const size_t index = static_cast<size_t>(page.GetTemplate());
	result.SetValue(ibT("template"), index < TemplateWords().size() ? TemplateWords()[index] : ibString(ibT("?")));

	const auto column = [&](ibHomePageColumn which, const ibString& name) {
		std::vector<ibDataValue> items;
		for (const ibHomePageItem& item : page.GetColumn(which))
			items.push_back(FormEntry(item.m_formId, forms));
		result.AddField(name, ibDataValue::Array(items));
	};
	column(eHomePageColumn_Left, ArgLeft().Name());
	column(eHomePageColumn_Right, ArgRight().Name());
}

class ibMcpToolHomePage : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("home_page"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("arranging the start page");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("THE START PAGE - the tab every session opens first: several forms at once, side by side, each "
			"a full runtime form with its module and events. It belongs to the application root, so every user sees "
			"it. Called with nothing, it READS the page: the template, each column's forms, and `forms` - every form "
			"the page can show (a shared form, or a form of a catalog, document, report...: a list form shows the list, "
			"a report's form the report, an object form a new object - what the object's own command would open). "
			"Give `left` / `right` to set a column - the forms top to bottom, replacing it - and `template` to split "
			"it otherwise; what is not given stays. WHAT BELONGS THERE - a data tool's form above all - is "
			"pattern_read {name: home-page}. A form that does not exist yet is made first (metadata_create of "
			"kind Form under its owner, with its FormType). The change is the application's like any other: app_save stores it, app_apply gives it to the "
			"base, and a running application shows it from its next start.");
	}

	ibString GetSearchText() const override
	{
		return ibMcpText("home page start page first tab desktop workspace dashboard columns forms layout showcase "
			"opens first welcome");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgTemplate(), ArgLeft(), ArgRight() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaDataApplicationBase* metaData = activeMetaData;

		if (metaData == nullptr || !metaData->IsOpen()) {
			refusal = ibMcpText("No application is open, so there is no start page to arrange.");
			return false;
		}

		ibValueMetaObjectApplication* const root = metaData->GetRootMetaObject();
		if (root == nullptr) {
			refusal = ibMcpText("This application has no root to hold a start page.");
			return false;
		}

		const std::map<ibMetaID, const ibValueMetaObjectFormBase*> forms = PlaceableForms(metaData);

		// READING - nothing given. The page, and every form it can show.
		if (!ArgTemplate().Given(params) && !ArgLeft().Given(params) && !ArgRight().Given(params)) {
			AnswerPage(root->GetHomePage(), forms, result);
			std::vector<ibDataValue> offered;
			for (const auto& one : forms)
				offered.push_back(FormEntry(one.first, forms));
			result.AddField(ibT("forms"), ibDataValue::Array(offered));
			return true;
		}

		if (!root->IsEditable()) {
			refusal = ibString::Format(ibMcpText("'%s' takes no edit here, so its start page stays as it is."),
				root->GetName());
			return false;
		}

		ibHomePageDescription page = root->GetHomePage();

		if (ArgTemplate().Given(params)) {
			const ibString word = ArgTemplate().Text(params);
			const auto found = std::find(TemplateWords().begin(), TemplateWords().end(), word);
			if (found == TemplateWords().end()) {
				refusal = ibString::Format(ibMcpText("'%s' is no template - one of oneColumn, twoEqualColumns, "
					"twoColumnsWideLeft, twoColumnsWideRight."), word);
				return false;
			}
			page.SetTemplate(static_cast<ibHomePageTemplate>(found - TemplateWords().begin()));
		}

		// A column given is replaced whole; an id the page cannot show refuses the call, and nothing changes.
		ibString notForms;
		const auto column = [&](const ibArg& arg, ibHomePageColumn which) {
			const auto many = params.FindField(arg.Name());
			if (!many || many->Kind() != ibDataKind::Array)
				return;
			std::vector<ibHomePageItem>& items = page.GetColumn(which);
			items.clear();
			for (const ibDataValue& one : many->AsArray()) {
				const ibMetaID id = one.Kind() == ibDataKind::Number ? (ibMetaID)one.AsInt() : ibNOT_FOUND;
				if (id == ibNOT_FOUND || forms.find(id) == forms.end()) {
					notForms += (notForms.IsEmpty() ? ibString() : ibString(ibT(", ")))
						+ (id == ibNOT_FOUND ? ibString(ibT("?")) : ibString::Format(ibT("%d"), (int)id));
					continue;
				}
				items.emplace_back(id);
			}
		};
		column(ArgLeft(), eHomePageColumn_Left);
		column(ArgRight(), eHomePageColumn_Right);

		if (!notForms.IsEmpty()) {
			refusal = ibString::Format(ibMcpText("Not a form the start page can show: %s. Nothing was changed - "
				"home_page with no arguments lists the forms it can."), notForms);
			return false;
		}

		const bool changed = !(root->GetHomePage() == page);
		if (changed) {
			root->SetHomePage(page);
			metaData->Modify(true);
		}

		AnswerPage(page, forms, result);
		result.SetValue(ibT("changed"), changed);
		return true;
	}
};

} // namespace

MCP_TOOL_REGISTER_STUDIO(ibMcpToolHomePage);
