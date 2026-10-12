#ifndef _FRMCLIENT_QUERY_EXPRESSION_DIALOG_H__
#define _FRMCLIENT_QUERY_EXPRESSION_DIALOG_H__

// THE EXPRESSION EDITOR — the desktop's ibDialogQueryExpression (frontend/win/dlgs/queryConstructor/
// queryExpressionDialog.h): the designer's, with the fields, the language's palette and the parser — which is the
// server's. Here with the desktop's public face, and it answers Cancel: the text it was given stays as it was.

#include <wx/dialog.h>

#include "frmclient/backend/query/queryConstructorModel.h"   // ibQueryConstructorField, ibQueryAstExprPtr, ibBalanceRole

#include <vector>

class ibMetaData;

// ONE WINDOW FOR A FIELD'S BALANCE ROLE — the desktop's, beside the expression editor. It answers Cancel: the role stays
// as it was said.
inline bool ibEditBalanceRole(wxWindow* WXUNUSED(parent), bool& WXUNUSED(said), ibBalanceRole& WXUNUSED(role),
	int& WXUNUSED(periodRank), bool WXUNUSED(readOnly) = false, bool WXUNUSED(withNumber) = true)
{
	return false;
}

class ibDialogQueryExpression : public wxDialog
{
public:
	ibDialogQueryExpression(wxWindow* WXUNUSED(parent), const wxString& WXUNUSED(title),
	                        const std::vector<ibQueryConstructorField>& WXUNUSED(fields),
	                        const ibQueryAstExprPtr& existing,
	                        const ibMetaData* WXUNUSED(metaData) = nullptr, bool WXUNUSED(readOnly) = false,
	                        bool WXUNUSED(allowWindows) = true)
		: m_expression(existing) {}

	virtual int ShowModal() override { return wxID_CANCEL; }

	ibQueryAstExprPtr GetExpression() const { return m_expression; }
	wxString          GetText() const { return m_text; }

	void SetText(const wxString& text) { m_text = text; }

	void OfferRole(bool said, ibBalanceRole role, int periodRank) {
		m_roleSaid = said;
		m_role = role;
		m_periodRank = periodRank;
	}
	bool          RoleSaid()   const { return m_roleSaid; }
	ibBalanceRole Role()       const { return m_role; }
	int           PeriodRank() const { return m_periodRank; }

private:
	ibQueryAstExprPtr m_expression;
	wxString          m_text;
	bool              m_roleSaid = false;
	ibBalanceRole     m_role = ibBalanceRole::None;
	int               m_periodRank = 0;
};

#endif
