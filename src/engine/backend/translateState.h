#ifndef __IB_TRANSLATE_STATE_H__
#define __IB_TRANSLATE_STATE_H__

// Per-session state of TRANSLATION — the third of a session's slots, beside the interpreter's (ibProcUnitState)
// and the compiler's (ibCompileState): every session may speak its own language, and one server may hold bases in
// different languages. A session holds one (ibSession::GetTranslateState); a thread without a session —
// codeRunner, a test — gets its own.

#include <wx/string.h>

struct ibTranslateState {
	wxString m_languageCode;            // the script's override (SetLanguageCode); empty — none
	wxString m_userLanguageCode;        // the user's, set at the login (SetUserInfo)
	wxString m_defLanguageCode;         // the configuration's main language (SetUserLanguage, CompileRoot)

	// THE ANSWER, kept ready — the hot path (ibBackendLocalization::GetUserLanguage) reads this one field: the
	// override, else the user's, else the configuration's. Every writer above calls Resolve.
	wxString m_resolvedLanguageCode;

	void Resolve() {
		m_resolvedLanguageCode = !m_languageCode.IsEmpty() ? m_languageCode
			: !m_userLanguageCode.IsEmpty() ? m_userLanguageCode
			: m_defLanguageCode;
	}
};

#endif
