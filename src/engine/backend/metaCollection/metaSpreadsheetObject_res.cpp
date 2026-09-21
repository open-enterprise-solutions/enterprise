#include "metaSpreadsheetObject.h"

/* PNG */
static const wxString s_grid_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABL0lEQVR4nOzbMQrCMBgF4NfiNRydBEHQRRe9gMfo7FGc6y28gC66KAiCk6OHcNTFgv0hDb8RBd/7tiYG8dG0/WOag1wOcgoA5Fq2YTCa3PHHDrtN9nqsMwAfVpZl7bgoCnikjvfSGRD7wH67hsfxfPnpeGs4njb26zYIcgoA5BQAyCkAkFMAIKdaINRRVWX22TxmtryZlhPSpI1fPX9HqKrUFAA5XQNCHdWc8dfj9Tl7XfTgYa85/W4HHu15/ftjK0qaAiCnAEBOAYCcAgA5BQByqgVCHe+uB1j22dwvbXyp9YBmCgDkMttg9wil/r/vredTx1t2f4D2CBkKAOQUAMgpAJBTACCnAEBOAYCcAgC56PsCsf32ln3j49vjvbQiZBv01hiZDOR0GwQ5+gAeAAAA///5K8GdAAAABklEQVQDAFAVTcMgOKcVAAAAAElFTkSuQmCC");

wxIcon ibValueMetaObjectSpreadsheetBase::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectSpreadsheetBase::GetIconGroup()
{
	static wxIcon icon = 
		ibBackendPicture::GetIconFromBase64(s_grid_16_png, wxSize(16, 16));

	return icon;
}