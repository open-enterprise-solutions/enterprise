////////////////////////////////////////////////////////////////////////////
//	Description : an external data tool or report, handed over and opened where the person sees it
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE PERSON'S ROAD, FILE → OPEN AND SAVE. The file is the client's; the server holds what was handed to it, by its
// name. Its bytes go into the session's temporary storage under that name (the protocol's upload), and the doc manager
// makes of it the document its extension says (open): in the studio the one that edits it (ibDataToolEditDocument,
// ibReportEditDocument) — a tab and a tree the person sees and can work in beside the assistant; in the application the
// one that runs it. A save is the person's Save: the document written into its temporary file, whose bytes go back to
// be kept where the caller keeps the file, as the client takes them down.
//
// ⚠ AND CLOSED AS THE PERSON CLOSES IT: the tab IS the external. Closed without a save, what was not saved is gone —
// for both of them; nothing here keeps a copy behind it.
//
////////////////////////////////////////////////////////////////////////////

#include "frmserver/mcp/mcpTool.h"

#include "frmserver/client/clientFrame.h"    // ibClientFrame — whether the application's run showed a window
#include "frmserver/docView/docView.h"
#include "frmserver/docView/docManager.h"   // ibDocTemplate — what makes a new one
#include "frmserver/docView/templates/studio/docViewDataToolEdit.h"
#include "frmserver/docView/templates/studio/docViewDataReportEdit.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/backend_mainFrame.h"
#include "backend/session/session.h"
#include "backend/temp/tempStorage.h"

#include "core/fileKind.h"
#include "core/fileSystem.h"
#include "core/utils/base64.h"

#include <vector>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// What a data tool and a report differ in here — the rest is one road.
struct ibDataToolKind {
	using Document = ibDataToolEditDocument;
	static constexpr ibFileKind s_file = ibFileKind::Tool;
	static ibString Open() { return ibT("external_data_tool_open"); }
	static ibString Save() { return ibT("external_data_tool_save"); }
	static ibString Word() { return ibMcpText("external data tool"); }
	static ibString Other() { return ibT("external_report_open"); }
};

struct ibReportKind {
	using Document = ibReportEditDocument;
	static constexpr ibFileKind s_file = ibFileKind::Report;
	static ibString Open() { return ibT("external_report_open"); }
	static ibString Save() { return ibT("external_report_save"); }
	static ibString Word() { return ibMcpText("external report"); }
	static ibString Other() { return ibT("external_data_tool_open"); }
};

const ibArg& ArgFile()
{
	static const ibArg s_a(ibT("file"), ibArg::Kind::Text,
		ibMcpText("The file's name with its extension, no folder - what the studio knows it by. Every other tool "
			"names this external by it, as `metadata`."), /*required*/ true);
	return s_a;
}

const ibArg& ArgData()
{
	static const ibArg s_a(ibT("data"), ibArg::Kind::Text,
		ibMcpText("The file's bytes in base64, read where you keep the file. Omitted in the studio: a new one is "
			"made under `file`."));
	return s_a;
}

// The window this call came from shows what it did: its session's frame, refreshed so a client at rest is called.
void ShowInWindow()
{
	if (ibSession* const session = ibSession::Current()) {
		if (ibBackendDocFrame* const frame = session->GetFrame())
			frame->RefreshFrame();
	}
}

// The file of this kind, or the refusal that says which tool opens what it is.
template <typename TKind>
bool IsOfKind(const ibString& file, ibString& refusal)
{
	ibFileKind kind;
	if (ibFileKindOf(file, kind) && kind == TKind::s_file)
		return true;
	refusal = ibString::Format(ibMcpText("'%s' is not an %s - its file ends in .%s (%s opens the other kind)."),
		file, TKind::Word(), ibFileExtension(TKind::s_file), TKind::Other());
	return false;
}

// What the answer says of an open one: the name it is known by, what it is called, and its root to start from.
void SayExternal(const ibMetaDataDocument* document, ibDataNode& result)
{
	const ibMetaData* const metaData = document->GetMetaData();
	const ibValueMetaObject* const root = metaData->GetRootMetaObject();
	result.SetValue(ibT("file"), metaData->GetFileName());
	if (root != nullptr) {
		result.SetValue(ibT("name"), root->GetName());
		result.AddField(ibT("id"), ibDataValue::Int((std::int64_t)root->GetMetaID()));
	}
	result.SetValue(ibT("modified"), document->IsModified());
}

} // namespace

//---------------------------------------------------------------------------
// external_data_tool_open / external_report_open
//---------------------------------------------------------------------------
template <typename TKind>
class ibMcpToolExternalOpen : public ibMcpTool {
public:

	ibString GetName() const override { return TKind::Open(); }

	// In the application it runs, and its code works with the records.
	bool IsDataAssistant() const override { return true; }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("opening an %s: %s"), TKind::Word(), ArgFile().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibString::Format(ibMcpText("Open an %s as File - Open does: its bytes in `data`, under its file name. "
			"IN THE STUDIO it gets its tab and its tree, so the person sees what you opened and can work in it too; "
			"without `data` a new one is made under that name. Change it with the tools that change the application - "
			"metadata_tree, metadata_create, metadata_set, module_write, form_*, sheet_* - passing `file` as "
			"`metadata`; its NodeIds are its own. %s hands its bytes back, to keep where the file is. The tab is the "
			"only copy: closed without a save, what was not saved is gone. IN THE APPLICATION it runs - its form opens "
			"in front of the person (`shown`; one with no default form runs its module and shows nothing) - and "
			"nothing in it is changed."), TKind::Word(), TKind::Save());
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgFile(), ArgData() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		const ibString file = ArgFile().Text(params);
		if (!IsOfKind<TKind>(file, refusal))
			return false;
		if (ibPathOf(file).has_parent_path()) {
			refusal = ibString::Format(ibMcpText("'%s' is a path - `file` is the file's name alone, as the studio "
				"knows it, and its bytes come in `data`."), file);
			return false;
		}

		const ibString data = ArgData().Text(params);
		const bool application = ibIsMcpInApplication();
		if (application && data.IsEmpty()) {
			refusal = ibString::Format(ibMcpText("The application runs the %s it is handed - its bytes in `data`."),
				TKind::Word());
			return false;
		}

		ibDocManager* const manager = docManager;
		ibSession* const session = ibSession::Current();
		ibTempStorage* const storage = session != nullptr
			? ibApplicationInstance::GetTempStorage(session->GetApplicationInstance()) : nullptr;
		if (manager == nullptr || storage == nullptr) {
			refusal = ibMcpText("There is no window to open it in.");
			return false;
		}

		// Open already — the person's or an earlier call's: that one, not a second copy beside it.
		if (ibMetaDataDocument* const open = ibMcpExternalNamed(file)) {
			SayExternal(open, result);
			result.SetValue(ibT("note"), ibMcpText("It was open already - this is that one as it stands, and `data` "
				"was not taken."));
			return true;
		}

		ibMetaDataDocument* document = nullptr;
		if (!data.IsEmpty()) {
			ibMemoryBuffer bytes;
			if (!ibBase64::Read(data, bytes) || bytes.GetDataLen() == 0) {
				refusal = ibMcpText("`data` did not decode as base64, or decoded to nothing.");
				return false;
			}
			// The protocol's upload: a file of the session's temporary storage under the name…
			const ibString id = storage->Create(session->Identity().m_guid, file);
			ibTempFile temp;
			if (id.IsEmpty() || !temp.Create(id) || !temp.Write(bytes.GetData(), bytes.GetDataLen())) {
				refusal = ibString::Format(ibMcpText("'%s' could not be handed over."), file);
				return false;
			}
			// …and its open: the document the name says. The application's runs it and is gone once it has
			// (ibDataToolFileDocument::DoOpenDocument) — its form stays, in front of the person. One with no
			// default form runs its module and shows nothing, and the answer says which it was.
			if (application) {
				const ibClientFrame* const window = ibClientFrame::GetFrame();
				const std::size_t tabs = window != nullptr ? window->TabCount() : 0;
				bool ran = true;
				try {
					const ibFailureToCaller failureToCaller(session);
					manager->CreateDocument(id);
				}
				catch (const ibBackendInterruptException&) {
					throw;
				}
				catch (const ibCoreException& thrown) {
					refusal = thrown.GetErrorDescription();
					ran = false;
				}
				ShowInWindow();
				if (!ran)
					return false;
				result.SetValue(ibT("file"), file);
				result.SetValue(ibT("ran"), true);
				result.SetValue(ibT("shown"), window != nullptr && window->TabCount() > tabs);
				return true;
			}
			document = dynamic_cast<ibMetaDataDocument*>(manager->CreateDocument(id));
			if (document == nullptr) {
				refusal = ibString::Format(ibMcpText("'%s' could not be opened - it is not an %s this studio reads; "
					"journal_read with `level: error` says why."), file, TKind::Word());
				return false;
			}
		}
		else {
			// A new one, as File - New makes it…
			ibDocTemplate* const temp = manager->FindTemplate(typeid(typename TKind::Document));
			ibDocument* const created = temp != nullptr ? temp->CreateDocument(ibEmptyString, ibDOC_NEW) : nullptr;
			typename TKind::Document* const fresh = dynamic_cast<typename TKind::Document*>(created);
			if (fresh == nullptr) {
				refusal = ibString::Format(ibMcpText("This studio makes no %s."), TKind::Word());
				return false;
			}
			fresh->SetDocumentName(temp->GetDocumentName());
			// …named as Save as names one (ibClientFrame::DoCommand): written into a file of the temporary storage
			// under the name. A Save writes there from now on, with no dialog asking where.
			const ibString id = fresh->OnNewDocument() ? storage->Create(session->Identity().m_guid, file) : ibString();
			if (id.IsEmpty() || !fresh->OnSaveDocument(id)) {
				fresh->DeleteAllViews();
				refusal = ibString::Format(ibMcpText("A new %s could not be made as '%s'."), TKind::Word(), file);
				return false;
			}
			fresh->SetTitle(file);
			document = fresh;
			result.SetValue(ibT("created"), true);
		}

		ShowInWindow();
		SayExternal(document, result);
		return true;
	}
};

//---------------------------------------------------------------------------
// external_data_tool_save / external_report_save
//---------------------------------------------------------------------------
template <typename TKind>
class ibMcpToolExternalSave : public ibMcpTool {
public:

	ibString GetName() const override { return TKind::Save(); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("saving an %s: %s"), TKind::Word(), ArgFile().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibString::Format(ibMcpText("Save an %s open in the studio, as the person's Save does, and hand its "
			"bytes back in `data` (base64) - write them as the file where you keep it. Its tab stops showing it as "
			"changed."), TKind::Word());
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgFile() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		const ibString file = ArgFile().Text(params);
		if (!IsOfKind<TKind>(file, refusal))
			return false;

		typename TKind::Document* const document = dynamic_cast<typename TKind::Document*>(ibMcpExternalNamed(file));
		if (document == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' is not open in the studio - %s opens it. A tab closed takes "
				"with it what was not saved."), file, TKind::Open());
			return false;
		}

		// The person's Save: the document written into its temporary file…
		if (!document->Save()) {
			refusal = ibString::Format(ibMcpText("'%s' could not be saved - journal_read with `level: error` says "
				"why."), file);
			return false;
		}

		// …whose bytes the client takes down; here they go back in the answer.
		const ibTempFile temp(document->GetFilename());
		ibMemoryBuffer bytes;
		ibMemoryBuffer part;
		for (int index = 0; temp.ReadPart(index, part); ++index)
			bytes.AppendData(part.GetData(), part.GetDataLen());
		if (bytes.GetDataLen() == 0) {
			refusal = ibString::Format(ibMcpText("'%s' was saved, and its file could not be read back."), file);
			return false;
		}

		ibString data;
		ibBase64::Append(data, bytes.GetData(), bytes.GetDataLen());
		SayExternal(document, result);
		result.SetValue(ibT("data"), data);
		return true;
	}
};

using ibMcpToolExternalDataToolOpen = ibMcpToolExternalOpen<ibDataToolKind>;
using ibMcpToolExternalDataToolSave = ibMcpToolExternalSave<ibDataToolKind>;
using ibMcpToolExternalReportOpen = ibMcpToolExternalOpen<ibReportKind>;
using ibMcpToolExternalReportSave = ibMcpToolExternalSave<ibReportKind>;

// The open is the studio's and the application's both, as File - Open is: the document its name says edits it in one
// and runs it in the other. The save is the studio's.
MCP_TOOL_REGISTER(ibMcpToolExternalDataToolOpen);
MCP_TOOL_REGISTER_STUDIO(ibMcpToolExternalDataToolSave);
MCP_TOOL_REGISTER(ibMcpToolExternalReportOpen);
MCP_TOOL_REGISTER_STUDIO(ibMcpToolExternalReportSave);
