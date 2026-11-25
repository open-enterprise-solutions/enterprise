#include "about.h"

#include <wx/clipbrd.h>

#include "protocol/protocol.h"
#include "frmclient/backend/appData.h"   // StudioMode — which of the two programs this is
#include "frmclient/win/picture.h"
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/staticBitmap.h"
#include "deskgui/univ/ctrl/staticLine.h"
#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/textCtrl.h"

namespace {

// WHAT THE PLATFORM IS BUILT ON — NOTICE.md's components the desktop carries, its drawing and its code editor first.
// Names, as their projects say them; the dot between them is UTF-8, said in bytes so the source stays ASCII.
const char* const s_builtOn =
	"wxWidgets \xC2\xB7 Scintilla \xC2\xB7 Mbed TLS \xC2\xB7 Asio \xC2\xB7 llhttp \xC2\xB7 Expat\n"
	"LodePNG \xC2\xB7 lunasvg \xC2\xB7 SQLite \xC2\xB7 Firebird \xC2\xB7 PostgreSQL";

const char* const s_licence = "\xC2\xA9 2026 Open Enterprise Solutions \xC2\xB7 PolyForm Noncommercial 1.0.0";

const char* const s_project = "https://github.com/open-enterprise-solutions/enterprise";

// A FACT'S ROW: its name, and the value as a borderless read-only field the text can be copied out of
// (ibTextCtrl::HasContentBackground — on the dialog's colour), as wide as its text; an empty value is a dash. The line
// goes to the facts Copy hands on.
void AddFact(wxWindow* parent, wxFlexGridSizer* grid, wxString& facts, const wxString& label, const wxString& value)
{
	const wxString shown = !value.IsEmpty() ? value : wxString::FromUTF8("\xE2\x80\x94");
	grid->Add(new ibStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);

	ibTextCtrl* field = new ibTextCtrl(parent, wxID_ANY, shown, wxDefaultPosition, wxDefaultSize,
		wxTE_READONLY | wxBORDER_NONE);
	field->SetMinSize(wxSize(field->GetTextExtent(shown).x + parent->FromDIP(8), -1));
	grid->Add(field, 1, wxEXPAND);

	facts << label << wxT(": ") << shown << wxT("\n");
}

} // namespace

ibDialogAbout::ibDialogAbout(wxWindow* parent, const ibProtocolNode& shown, int id)
	: ibDialog(parent, id, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
	// WHICH PROGRAM — the studio or the workspace, as the frame logged in; the brand is not translated.
	const wxString program = wxT("OES ") + (appData->StudioMode() ? _("Application Studio") : _("Application Workspace"));
	SetTitle(wxString::Format(_("About %s"), program));

	// The platform's version and its build — the server's, the engine a report names.
	const wxString version = wxString::Format(_("Version %s, build %d"),
		shown.GetString(ibProtocol::ibSchema::ibAbout::Version),
		static_cast<int>(shown.GetInt(ibProtocol::ibSchema::ibAbout::Build)));

	const wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	wxFont programFont = font;
	programFont.SetPointSize(font.GetPointSize() + 5);
	programFont.MakeBold();

	const int pad = FromDIP(6);
	const int gap = FromDIP(2);

	wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

	// --- The program beside the logo ---
	wxBoxSizer* head = new wxBoxSizer(wxHORIZONTAL);
	const wxBitmap logo = ibProtocolPicture(shown, ibProtocol::ibSchema::ibAbout::Picture, 48);
	if (logo.IsOk()) {
		const wxBitmapBundle logos = wxBitmapBundle::FromBitmaps(logo,
			ibProtocolPicture(shown, ibProtocol::ibSchema::ibAbout::Picture, 96));
		head->Add(new ibStaticBitmap(this, wxID_ANY, logos), 0, wxALIGN_TOP | wxRIGHT, pad * 2);
	}
	wxBoxSizer* title = new wxBoxSizer(wxVERTICAL);
	ibStaticText* programText = new ibStaticText(this, wxID_ANY, program);
	programText->SetFont(programFont);
	title->Add(programText);
	title->Add(new ibStaticText(this, wxID_ANY, version), 0, wxTOP, gap);
	title->Add(new ibStaticText(this, wxID_ANY, _("Business applications from metadata and a language of their own")),
		0, wxTOP, pad);
	head->Add(title, 1, wxALIGN_CENTER_VERTICAL);
	root->Add(head, 0, wxEXPAND | wxALL, pad * 2);

	root->Add(new ibStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, pad * 2);

	// --- The facts: the base (none, no row), the connection, the person, the language, this computer, the plugins
	// loaded (a row only when there are any) ---
	m_facts << program << wxT("\n") << version << wxT("\n");
	wxFlexGridSizer* grid = new wxFlexGridSizer(0, 2, gap, pad * 2);
	grid->AddGrowableCol(1, 1);

	const wxString database = shown.GetString(ibProtocol::ibSchema::ibAbout::Database);
	if (!database.IsEmpty())
		AddFact(this, grid, m_facts, _("Database"), database);
	AddFact(this, grid, m_facts, _("Connection"), shown.GetString(ibProtocol::ibSchema::ibAbout::Program));
	AddFact(this, grid, m_facts, _("User"), shown.GetString(ibProtocol::ibSchema::ibAbout::User));
	AddFact(this, grid, m_facts, _("Language"), shown.GetString(ibProtocol::ibSchema::ibAbout::Locale));
	AddFact(this, grid, m_facts, _("System"), wxGetOsDescription());

	wxString plugins;
	for (const ibProtocolNode& plugin : shown.FindChild(ibProtocol::ibSchema::ibAbout::Plugins).Children()) {
		if (!plugins.IsEmpty())
			plugins << wxT(", ");
		plugins << plugin.GetString(ibProtocol::ibSchema::ibAbout::Name) << wxT(" ")
			<< plugin.GetString(ibProtocol::ibSchema::ibAbout::Version);
	}
	if (!plugins.IsEmpty())
		AddFact(this, grid, m_facts, _("Plugins"), plugins);
	m_facts.Trim();

	root->Add(grid, 0, wxEXPAND | wxALL, pad * 2);

	root->Add(new ibStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, pad * 2);

	// --- What it is built on, whose ideas it grew from, the licence and where the project lives ---
	wxBoxSizer* credits = new wxBoxSizer(wxVERTICAL);
	ibStaticText* builtOnCaption = new ibStaticText(this, wxID_ANY, _("Built on"));
	builtOnCaption->SetFont(font.Bold());
	credits->Add(builtOnCaption);
	credits->Add(new ibStaticText(this, wxID_ANY, wxString::FromUTF8(s_builtOn)), 0, wxTOP, gap);
	credits->Add(new ibStaticText(this, wxID_ANY,
		_("Grown from wxFormBuilder (the form editor) and the ideas of 2C (the interpreter)")), 0, wxTOP, pad);
	credits->Add(new ibStaticText(this, wxID_ANY, wxString::FromUTF8(s_licence)), 0, wxTOP, pad);

	ibStaticText* link = new ibStaticText(this, wxID_ANY, wxT("github.com/open-enterprise-solutions/enterprise"));
	link->SetHyperlink();
	link->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { wxLaunchDefaultBrowser(s_project); });
	credits->Add(link, 0, wxTOP, gap);

	root->Add(credits, 0, wxEXPAND | wxALL, pad * 2);

	// --- Copy, OK ---
	wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->AddStretchSpacer();
	ibButton* copy = new ibButton(this, wxID_ANY, _("Copy"));
	copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(m_facts));
			wxTheClipboard->Close();
		}
	});
	buttons->Add(copy, 0, wxRIGHT, pad);
	ibButton* ok = new ibButton(this, wxID_OK);
	ok->SetDefault();
	buttons->Add(ok);
	root->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad * 2);

	SetSizerAndFit(root);
	Centre(wxBOTH);

	// The title bar's icon — the same picture.
	const wxBitmap icon = ibProtocolPicture(shown, ibProtocol::ibSchema::ibAbout::Picture);
	if (icon.IsOk()) {
		wxIcon dialogIcon;
		dialogIcon.CopyFromBitmap(icon);
		SetIcon(dialogIcon);
	}
	ok->SetFocus();
}
