#ifndef _FRMCLIENT_BACKEND_META_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_OBJECT_H__

#include "frmclient/backend/metaData.h"   // the link — the tab the object was opened from
#include "frmclient/backend/propertyManager/propertyObject.h"   // a metaobject IS a property object, as the engine's

#include "core/clsid.h"

//ADVANCED OBJECTS
constexpr ibClassID g_metaAttributeCLSID = metadata_to_clsid("MD_ATTR");
constexpr ibClassID g_metaRoleCLSID = metadata_to_clsid("MD_ROLE");

// ⭐ A METAOBJECT OF THE CONFIGURATION — the engine's ibValueMetaObject (backend/metaCollection/metaObject.h), as the
// thin client holds it: ONE CLASS for every kind, no heir. What it is made of — its properties, how they read, what they
// may be set to — is the server's to say (Max, 2026-10-14): it holds only its id and the link to the configuration it
// was opened from (ibMetaData: the tab — two bases open, an edit goes to the one it came from), and fills its
// properties from the server's rows (ibPropertySerializable), one per row, kept — an edit running through one outlives the
// answer that brings the rows back. The designer's tree makes one for the row it selects, and drops it for the next
// (ibMetaTreeBase::SelectItem); a ported window that only names the class meets none.
class FRMCLIENT_API ibValueMetaObject : public ibPropertyObject {
public:

	ibValueMetaObject(ibMetaID id, const ibMetaData& metaData) : m_metaId(id), m_metaData(metaData) {}

	template <typename T>
	T* ConvertToType() const { return nullptr; }

	ibMetaID GetMetaID() const { return m_metaId; }
	wxString GetName() const;
	wxString GetSynonym() const;
	wxBitmap GetIcon() const { return wxNullBitmap; }

	// ITS TWO TEXTS, the engine's — help, what its user is told, and the note, what its designers are: the server's (schema
	// ObjectText), read when asked and given back at once.
	wxString GetHelpContent() const;
	void SetHelpContent(const wxString& strHelpContent);

	wxString GetNoteContent() const;
	void SetNoteContent(const wxString& strNoteContent);

	// ⭐ AN EDIT GOES TO THE SERVER — its property of that name given that value (as the property's node holds it), on the
	// object of this id in the configuration it was opened from: schema Properties, Save. False: refused.
	bool Send(const wxString& name, const ibDataValue& value) const;
	// …the value an edit in the inspector gives, the node its variant holds.
	bool Send(const wxString& name, const wxVariant& value) const;

	virtual wxString GetClassName() const override { return wxT("metaObject"); }
	virtual wxString GetObjectTypeName() const override { return wxT("metaObject"); }
	virtual bool IsEditable() const override { return true; }

	virtual const ibMetaData* GetMetaData() const override { return &m_metaData; }
	virtual ibMetaData* GetMetaData() override { return &m_metaData; }

	// Its properties as the server has them now — those received for it at this frame (below), else one batch (schema
	// Properties: Id, Tab), in place of the ones it holds. False: the server answered none.
	virtual bool Fetch() override;

	// THE INSPECTOR SETS THE VALUE; the object is told. Before — the value it is to take, sent (Send): the server's answer
	// is the leave, refused the value is not set. After — set: whoever shows it is told it is to be read anew (Cleared).
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

	// ⭐ THE ROWS OF THE OBJECT OF THAT TAB AND ID, RECEIVED WITHOUT ASKING — shown beside an answer (an edit's, a new
	// object's: the frame's Properties), or read before: true while the frame stays (ibFrontendMainFrame::GetFrameNumber),
	// so an object shown again — clicked again, walked back to — takes them in instead of asking. By the tab and the id: a
	// row of the tree makes its object anew each time it is selected. Taken in by its next Receive, not at once: an edit is
	// sent from inside the grid's change, and taking rows in remakes the properties the grid is still editing.
	static void Receive(long long tabId, ibMetaID metaId, const ibProtocolNode& rows);

private:

	// …each of its rows, the server's property (ibPropertySerializable).
	virtual ibProperty* CreateProperty(ibPropertyCategory* cat, ibDataNode&& row) override;

	const ibMetaID m_metaId;
	ibMetaData m_metaData;
};

#endif
