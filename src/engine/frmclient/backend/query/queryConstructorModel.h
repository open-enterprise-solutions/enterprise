#ifndef _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__
#define _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__

// THE QUERY CONSTRUCTOR'S MODEL — the engine's (backend/query/queryConstructorModel.h): what may go into a query, its
// sources and their fields, asked of the configuration — the server's. What the client's windows take of it is the
// answer: the fields a query text offers (ibQueryConstructorField, the core's), as the server described them with the
// window's question.

#include "frmclient/backend/backend_core.h"   // FRMCLIENT_API
#include "core/query/anyQueryConstructorModel.h"   // ibAnyQueryConstructorModel — what the model derives
#include "core/query/querySourceParameter.h"       // ibQuerySourceParameter — a virtual table's argument
#include "queryable.h"                            // ibSourceMetaDataScope, ibPeriodUnits — the desktop's header brings them (queryableFactory.h)
#include "frmclient/backend/compiler/value.h"     // ibValue — likewise

#include <memory>
#include <vector>

class ibMetaData;

// ⭐⭐ WHAT FIELDS A QUERY TEXT OFFERS — on the client, as the server said: parsing the text and asking the configuration
// are its, so it described the fields with the window's question (Fields — a field's name, its source's caption, its
// type, whether the options leave it). A configuration that is a tab's (a composer's panel, ibMetaData::GetTabId) asks
// that tab for `text` — the server parses it; `error` is why it offers none. Otherwise `text` is not read: the fields
// are the question's.
FRMCLIENT_API std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const ibString& text,
	const ibMetaData* metaData, ibString* error = nullptr);

// …AND WHAT A TAB'S VIEW ALREADY SAID OF A TEXT — a composer's tab draws its query's Fields and References with the view
// (and Error): kept here by the tab, so the panel's read of that same text asks nothing; forgotten with the tab.
class ibProtocolNode;
FRMCLIENT_API void ibQueryFieldsKnown(long long tabId, const ibString& text, const ibProtocolNode& answer);
FRMCLIENT_API void ibQueryFieldsForget(long long tabId);

// A DYNAMIC LIST'S FIELDS on a tab — those of its main table (wxNOT_FOUND: none) and what its query adds, as the desktop's
// list settings window reads them; asked of the tab (fetch {Tab, Request: {Kind: Source, Id, Text}}) once per main table
// and text. Off a tab: none.
FRMCLIENT_API std::vector<struct ibSettingsPlainField> ibListSourceFields(ibMetaID mainTable, const ibString& text,
	const ibMetaData* metaData, ibString* error = nullptr);

// ⭐⭐ THE DESKTOP'S MODEL, UNDER ITS OWN NAME — the windows of win/dlgs/queryConstructor are the desktop's, ported whole,
// and they ask this class what they ask there. What only the configuration answers is asked of the tab the window was
// opened from (ibProtocol::ibView::ibQueryConstructor): the sources, a source's fields, a virtual table's parameters —
// and a reference's fields one level down, the composer's Expand. Everything the model DERIVES from those (qualified
// fields, the walk of a path, the fields of a select) is the core's (ibAnyQueryConstructorModel), the desktop's too.
// What a type refers to the client does not know by itself: the server describes the references with every answer
// (References), and they are asked back from there (ibValueReferenceDataObject::ConvertToMetaIds).
// ONE QUESTION PER SOURCE: the answers are kept for the model's life — the window's.
class FRMCLIENT_API ibQueryConstructorModel : public ibAnyQueryConstructorModel
{
public:
	// `metaData` — the config the query runs on behalf of (its factory resolves the sources).
	// Null falls back to the global factory, which is the right answer with no config open.
	explicit ibQueryConstructorModel(const ibMetaData* metaData = nullptr);

	// EVERY source a query may name, grouped by nothing — the caller groups by m_path[0], which is
	// the metaclass kind, so the tree's top level is whatever the factory actually holds.
	std::vector<ibQueryConstructorSource> GetSources() const;

	// A kind's class picture, as the server named it (the Sources answer's Kinds); empty: none.
	ibString KindPicture(const ibString& kind) const;

	// The desktop's ibQueryLowering::CheckNames and UngroupedProjections, answered by the server (request Check).
	void CheckNames(const ibQueryPackage& package) const;
	std::vector<ibQueryAstExprPtr> UngroupedProjections(const ibQueryPackage& package, size_t statement,
	                                                    int unionBranch) const;

	// WHAT ARGUMENTS THIS SOURCE TAKES, in order — empty for an ordinary table, non-empty for a
	// virtual one (a register's balance / turnovers, an information register's slice). The shell
	// shows a parameter window only when this answers something, so a table that takes none never
	// grows a menu item about parameters it does not have.
	std::vector<ibQuerySourceParameter> GetSourceParameters(const ibQuerySource& source) const;

	// THE FIELDS A CONDITION PARAMETER MAY NAME — asked of the source, not decided here. A balance
	// answers with its dimensions only (a resource it folded cannot be filtered before folding); a
	// slice answers with everything it carries. Same shape as GetFields, so the expression editor
	// takes it without knowing which source it came from.
	// Fields admissible in a condition slot. `slot` empty = the general `Condition`; a named slot may
	// narrow it (an accounting register's account conditions admit ACCOUNTS only — the source consumes
	// them and folds by them, so a leaf about anything else cannot be applied at all).
	std::vector<ibQueryConstructorField> GetConditionFields(const ibQuerySource& source,
	                                                       const ibString& slot = ibEmptyString) const;

	// A TABLE'S OWN REFERENCE TYPE and the table a reference names — the server's, written with the catalogue
	// (request Sources: a table's Reference); 0 / empty: not one there.
	virtual ibClassID ReferenceOfTable(const std::vector<ibString>& path) const override;
	virtual ibString TableOfReference(ibClassID reference) const override;

protected:

	// A TABLE'S FIELDS, asked of the tab: by the source as written (request Fields, its arguments with it), by its id
	// (the composer's Expand).
	virtual std::vector<ibQueryConstructorField> GetTableFields(const ibQuerySource& source) const override;
	virtual std::vector<ibQueryConstructorField> GetTableFields(ibMetaID table) const override;
	// What the server said of the references with what it sent (ibValueReferenceDataObject::ConvertToMetaIds).
	virtual std::vector<ibMetaID> ConvertToMetaIds(const std::vector<ibClassID>& clsids) const override;

private:
	const ibMetaData* m_metaData = nullptr;   // which configuration the question is for — the server answers it
	std::shared_ptr<struct ibQueryModelAnswers> m_known;   // what the tab already answered — kept for the window's life
};

#endif
