#include "userItem.h"

#include "frmclient/backend/userInfo.h"   // the user — the server's schema behind the desktop's calls
#include "frmclient/backend/backend_picture.h"
#include "frmclient/backend/metaCollection/metaObject.h"   // g_metaRoleCLSID - the class whose picture a role shows
#include "core/picturePredefined.h"

bool ibDialogUserItem::ReadUserData(const ibGuid& userGuid, bool copy)
{
	if (m_userGuid.isValid())
		return false;

	const ibUserInfo userInfo = ibUserInfo::Read(userGuid);
	if (userInfo.IsOk()) {

		m_textName->SetValue(userInfo.m_strUserName);
		m_textFullName->SetValue(userInfo.m_strUserFullName);

		// The box stays empty: a stored password is said by the hint, not by text that could
		// be typed. Untouched, OK keeps the hash that is stored. A copy starts without a
		// password; it does not inherit the hash.
		m_bPasswordTouched = false;
		m_textPassword->SetHint(!copy && userInfo.IsSetPassword()
			? _("The password is set - type a new one to change it") : wxString());

		if (userInfo.IsSetLanguage()) {

			auto iterator = std::find_if(m_languageArray.begin(), m_languageArray.end(),
				[userInfo](const auto& pair) { return userInfo.m_strLanguageGuid == pair.second.m_strLanguageGuid; });

			// A language the configuration no longer has: the configuration's own language, as the desktop's; none
			// marked, the first one.
			if (iterator != m_languageArray.end()) {
				m_choiceLanguage->SetSelection(iterator->first);
			}
			else if (!m_strDefaultLanguageCode.IsEmpty()) {
				const wxString& strLanguageCode = m_strDefaultLanguageCode;
				auto iterator_active_metadata = std::find_if(m_languageArray.begin(), m_languageArray.end(),
					[strLanguageCode](const auto& pair) { return strLanguageCode == pair.second.m_strLanguageCode; });
				m_choiceLanguage->SetSelection(iterator_active_metadata != m_languageArray.end() ? iterator_active_metadata->first : 0);
			}
			else {
				m_choiceLanguage->SetSelection(0);
			}
		}

		for (const auto& role : userInfo.m_roleArray) {   // by reference — a role is a struct, not a handle

			auto iterator = std::find_if(m_roleArray.begin(), m_roleArray.end(),
				[role](const auto& pair) { return role.m_strRoleGuid == pair.second.m_strRoleGuid; });

			if (iterator != m_roleArray.end())
				m_choiceRole->Check(iterator->first, true);
		}

		if (!copy)
			m_userGuid = userInfo.m_strUserGuid;

		return true;
	}

	return false;
}

ibDialogUserItem::ibDialogUserItem(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style), m_bInitialized(false), m_bPasswordTouched(false)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_mainNotebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS);
	m_mainNotebook->SetArtProvider(new wxAuiLunaTabArt());
	m_main = new wxPanel(m_mainNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);

	wxBoxSizer* sizerUser = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizerUserTop = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* sizerLabel = new wxBoxSizer(wxVERTICAL);

	m_staticName = new wxStaticText(m_main, wxID_ANY, _("Name:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticName->Wrap(-1);
	sizerLabel->Add(m_staticName, 0, wxALL, FromDIP(9));

	m_staticFullName = new wxStaticText(m_main, wxID_ANY, _("Full name:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticFullName->Wrap(-1);
	sizerLabel->Add(m_staticFullName, 0, wxALL, FromDIP(9));

	sizerUserTop->Add(sizerLabel, 0, 0, FromDIP(5));

	wxBoxSizer* sizerText = new wxBoxSizer(wxVERTICAL);
	m_textName = new wxTextCtrl(m_main, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	sizerText->Add(m_textName, 0, wxALL | wxEXPAND, FromDIP(5));
	m_textFullName = new wxTextCtrl(m_main, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	sizerText->Add(m_textFullName, 0, wxALL | wxEXPAND, FromDIP(5));
	sizerUserTop->Add(sizerText, 1, wxEXPAND, FromDIP(5));
	sizerUser->Add(sizerUserTop, 0, wxEXPAND, FromDIP(5));

	m_staticline = new wxStaticLine(m_main, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
	sizerUser->Add(m_staticline, 0, wxALL | wxEXPAND, FromDIP(5));

	wxBoxSizer* sizerUserBottom = new wxBoxSizer(wxHORIZONTAL);
	m_staticPassword = new wxStaticText(m_main, wxID_ANY, _("Password:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticPassword->Wrap(-1);
	sizerUserBottom->Add(m_staticPassword, 0, wxALL, FromDIP(10));
	m_textPassword = new wxTextCtrl(m_main, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	sizerUserBottom->Add(m_textPassword, 1, wxALL, FromDIP(5));

	sizerUser->Add(sizerUserBottom, 1, wxEXPAND, FromDIP(5));

	m_main->SetSizer(sizerUser);
	m_main->Layout();
	sizerUser->Fit(m_main);
	m_mainNotebook->AddPage(m_main, _("User"), false, wxNullBitmap);
	m_other = new wxPanel(m_mainNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);

	wxBoxSizer* sizerOther = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* sizerLabels = new wxBoxSizer(wxVERTICAL);

	m_staticRole = new wxStaticText(m_other, wxID_ANY, _("Role:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticRole->Wrap(-1);
	m_staticLanguage = new wxStaticText(m_other, wxID_ANY, _("Language:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticLanguage->Wrap(-1);

	wxImageList* imageList = new wxImageList(16, 16);

	m_choiceRole = new ibCheckTree(m_other, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HIDE_ROOT | wxTR_ROW_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_choiceRole->AssignImageList(imageList);

	const wxTreeItemId& root = m_choiceRole->AddRoot(wxT(""));

	m_choiceLanguage = new wxChoice(m_other, wxID_ANY, wxDefaultPosition, wxDefaultSize);

	// The configuration's roles and languages — the server's, listed beside the users (the desktop's read them off the
	// active configuration). A role's picture is its class's (ibValueMetaObjectRole::GetIcon is GetIconGroup), asked of
	// the server by the class id once and kept by the client (ibProtocolPicture). One picture, so one image for all.
	const wxBitmap picture = ibBackendPicture::GetPicture(g_metaRoleCLSID);
	const int image = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;
	for (const ibUserInfo::RoleBrief& role : ibUserInfo::ListRoles()) {
		ibDataUserRole entry;
		entry.m_strRoleGuid = role.m_strRoleGuid;
		entry.m_strRoleName = role.m_strRoleName;
		entry.m_miRoleId = wxNOT_FOUND;
		entry.m_mode = role.m_mode;
		const wxTreeItemId& id = m_choiceRole->AppendItem(root, role.m_strRoleTitle, image, image);
		m_choiceRole->SetItemState(id, ibCheckTree::UNCHECKED);
		m_roleArray.insert_or_assign(id, entry);
	}

	// The base's users choose among the session's configuration's languages — the one they log in to.
	for (const ibUserInfo::LanguageBrief& language : ibUserInfo::ListLanguages(nullptr)) {

		ibDataUserLanguageItem entry;
		entry.m_strLanguageGuid = language.m_strLanguageGuid;
		entry.m_strLanguageCode = language.m_strLanguageCode;
		if (language.m_default)
			m_strDefaultLanguageCode = language.m_strLanguageCode;

		const int index = m_choiceLanguage->Append(language.m_strLanguageTitle);
		m_languageArray.insert_or_assign(index, std::move(entry));
	}

	m_choiceLanguage->SetSelection(0);

	sizerLabels->Add(m_staticRole, 0, wxEXPAND, FromDIP(5));
	sizerLabels->Add(m_choiceRole, 1, wxEXPAND, FromDIP(5));
	sizerOther->Add(sizerLabels, 1, wxEXPAND, FromDIP(5));

	wxBoxSizer* sizerChoice = new wxBoxSizer(wxVERTICAL);

	sizerChoice->Add(m_staticLanguage, 0, wxEXPAND, FromDIP(5));
	sizerChoice->Add(m_choiceLanguage, 0, wxEXPAND, FromDIP(5));

	sizerOther->Add(sizerChoice, 0, wxEXPAND, FromDIP(5));
	m_other->SetSizer(sizerOther);

	m_mainNotebook->AddPage(m_other, _("Other"), true, wxNullBitmap);
	mainSizer->Add(m_mainNotebook, 1, wxEXPAND | wxALL, FromDIP(5));

	m_bottom = new wxStdDialogButtonSizer();
	m_bottomOK = new wxButton(this, wxID_OK);
	m_bottom->AddButton(m_bottomOK);
	m_bottomCancel = new wxButton(this, wxID_CANCEL);
	m_bottom->AddButton(m_bottomCancel);
	m_bottom->Realize();

	m_mainNotebook->SetSelection(0);
	mainSizer->Add(m_bottom, 0, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picUserCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();

	// Connect Events
	m_textName->Bind(wxEVT_COMMAND_TEXT_ENTER,
		[&](wxCommandEvent& event) {
			m_textFullName->SetValue(stringUtils::GenerateSynonym(event.GetString()));
			event.Skip();
		}
	);

	m_textPassword->Bind(wxEVT_COMMAND_TEXT_UPDATED,
		[&](wxCommandEvent& event) {
			// Remember that the box was edited. The hash is computed once, by the server on OK;
			// a box filled with m_bInitialized false must not count.
			if (m_bInitialized)
				m_bPasswordTouched = true;

			event.Skip();
		}
	);

	m_bottomOK->Bind(wxEVT_COMMAND_BUTTON_CLICKED,
		[&](wxCommandEvent& event) {

			if (m_textName->IsEmpty())
				return;

			if (!m_userGuid.isValid())
				m_userGuid = wxNewUniqueGuid;

			ibUserInfo userInfo;

			userInfo.m_strUserGuid = m_userGuid.str();
			userInfo.m_strUserName = m_textName->GetValue();
			userInfo.m_strUserFullName = m_textFullName->GetValue();
			// The password typed goes to the server to be hashed — only when it was touched.
			userInfo.m_bPasswordTouched = m_bPasswordTouched;
			userInfo.m_strUserPassword = m_textPassword->GetValue();

			const int selection = m_choiceLanguage->GetSelection();
			if (selection != wxNOT_FOUND) {
				const ibDataUserLanguageItem& info = m_languageArray.at(selection);
				userInfo.m_strLanguageGuid = info.m_strLanguageGuid;
				userInfo.m_strLanguageName = info.m_strLanguageName;
				userInfo.m_strLanguageCode = info.m_strLanguageCode;
			}

			const wxTreeItemId& root = m_choiceRole->GetRootItem();

			wxTreeItemIdValue coockie;
			wxTreeItemId item = m_choiceRole->GetFirstChild(root, coockie);

			while (item.IsOk()) {
				if (ibCheckTree::CHECKED == m_choiceRole->GetItemState(item)) {
					const ibDataUserRole& info = m_roleArray.at(item);
					auto& entry = userInfo.m_roleArray.emplace_back();
					entry.m_strRoleGuid = info.m_strRoleGuid;
					entry.m_strRoleName = info.m_strRoleName;
					entry.m_miRoleId = info.m_miRoleId;
					entry.m_mode = info.m_mode;
				}
				item = m_choiceRole->GetNextChild(root, coockie);
			}

			// That some user keeps a role with the administration rights is the server's check — refused with the
			// desktop's text, shown to the person by the call.
			if (!ibUserInfo::Save(userInfo))
				return;

			event.Skip();
		}
	);

	m_bInitialized = true;
}
