#ifndef _FRMCLIENT_BACKEND_COMMON_OBJECT_H__
#define _FRMCLIENT_BACKEND_COMMON_OBJECT_H__

#include <algorithm>
#include <map>
#include <vector>

#include "core/guid.h"
#include "core/description/predefinedDescription.h"   // ibPredefinedDescription — what an item of the kind is entered with
#include "core/tabular/tabularModelView.h"   // ibDataViewObject — a predefined value is a row of the predefined editor

#include "frmclient/backend/backend_picture.h"
#include "core/picturePredefined.h"
#include "frmclient/backend/metaCollection/metaObject.h"

// THE ENGINE'S HIERARCHICAL REFERENCE OBJECT (backend/metaCollection/partial/commonObject.h) as far as the predefined
// values editor asks it (designer-thin/win/dlg/predefinedEditor.h): a catalog's, a chart's — the object of that id in
// the configuration it was opened from, its predefined items the server's (ibProtocol::ibSchema::ibKind::Predefined), read when the
// editor opens and after each of its edits.
class FRMCLIENT_API ibValueMetaObjectRecordDataHierarchyMutableRef : public ibValueMetaObject {
public:
	using ibValueMetaObject::ibValueMetaObject;

	// AN ITEM AS A ROW OF THE EDITOR'S TREE — the item as the server keeps it (ibPredefinedItemDescription: what it
	// declares, each value as it travels, held as it comes and sent back as it is held), and the row it stands under.
	class ibPredefinedItem : public ibDataViewObject {
	public:

		// Folder/leaf flag is stored on the predefined value itself
		// — the designer's predefined-list dialog renders these as
		// a tree, so the data-view item asks the row "are you a
		// container?" directly.  Keeps the dialog free of model-side
		// IsContainer overrides.
		virtual bool IsContainer() const override { return m_item.m_folder; }

		explicit ibPredefinedItem(const ibPredefinedItemDescription& item) : m_item(item) {}

		ibGuid GetPredefinedGuid() const { return m_item.m_guid; }

		ibString GetPredefinedParentName() const {
			if (m_valueParent != nullptr)
				return m_valueParent->GetPredefinedName();
			return ibT("");
		}

		const ibString& GetPredefinedName() const { return m_item.m_name; }
		// The code and the description as they read — each a value of its field's type (#646).
		const ibString& GetPredefinedCode() const { return m_item.m_code.m_title; }
		const ibString& GetPredefinedDescription() const { return m_item.m_description.m_title; }

		bool IsPredefinedFolder() const { return m_item.m_folder; }
		wxObjectDataPtr<ibPredefinedItem> GetPredefinedParent() const { return m_valueParent; }

		const ibPredefinedRowDescription& GetPredefinedRow() const { return m_item.m_row; }
		const std::map<ibMetaID, ibPredefinedRowDescriptionVector>& GetPredefinedTables() const { return m_item.m_tables; }

		friend class ibValueMetaObjectRecordDataHierarchyMutableRef;

	private:

		ibPredefinedItemDescription m_item;
		wxObjectDataPtr<ibPredefinedItem> m_valueParent;
	};

	//is predefined value?
	bool HasPredefinedValue(const ibGuid& valueGuid) const { return FindPredefinedValue(valueGuid) != nullptr; }
	bool HasPredefinedValue(const ibString& strPredefinedName) const { return FindPredefinedValue(strPredefinedName) != nullptr; }

	// An item made, or the item of its Guid given this one (none of it: one more, of that Guid) — in the node form of the
	// one schema, in one command, which the server takes whole or refuses. False: refused, and the person told why.
	bool AppendPredefinedValue(const ibPredefinedItemDescription& item);
	bool SetPredefinedValue(const ibPredefinedItemDescription& item);

	void DeletePredefinedValue(const ibGuid& predefinedGuid);

	//find predefined value
	wxObjectDataPtr<ibPredefinedItem> FindPredefinedValue(const ibGuid& predefinedGuid) const {

		auto iterator = std::find_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedGuid](const auto& value) { return predefinedGuid == value->GetPredefinedGuid(); });

		if (iterator != m_predefinedObjectVector.end())
			return *iterator;

		return wxObjectDataPtr<ibPredefinedItem>();
	}

	wxObjectDataPtr<ibPredefinedItem> FindPredefinedValue(const ibString& predefinedName) const {

		auto iterator = std::find_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedName](const auto& value) { return predefinedName == value->GetPredefinedName(); });

		if (iterator != m_predefinedObjectVector.end())
			return *iterator;

		return wxObjectDataPtr<ibPredefinedItem>();
	}

	//predefined values
	const std::vector<wxObjectDataPtr<ibPredefinedItem>>& GetPredefinedValueArray() const { return m_predefinedObjectVector; }

	// THE ONE SCHEMA, as the server said it (the engine's GetPredefinedDesc): the items and what one is entered with —
	// the window is built of it, its sections and their columns whatever the server says this time.
	const ibPredefinedDescription& GetPredefinedDesc() const { return m_predefinedDesc; }

	// …as the server's editor holds them (ibProtocol::ibSchema::ibKind::Predefined), read anew — when the editor opens and after
	// each of its edits; the array keeps what was read till then.
	void ReadPredefinedValues();

private:
	std::vector<wxObjectDataPtr<ibPredefinedItem>> m_predefinedObjectVector;   // the schema's items, as rows of the tree
	ibPredefinedDescription m_predefinedDesc;
};

#endif
