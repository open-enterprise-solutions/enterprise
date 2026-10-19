#ifndef _FRMCLIENT_BACKEND_COMMON_OBJECT_H__
#define _FRMCLIENT_BACKEND_COMMON_OBJECT_H__

#include <algorithm>
#include <vector>

#include "core/guid.h"
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

	class ibPredefinedValueObject : public ibDataViewObject {
	public:

		// Folder/leaf flag is stored on the predefined value itself
		// — the designer's predefined-list dialog renders these as
		// a tree, so the data-view item asks the row "are you a
		// container?" directly.  Keeps the dialog free of model-side
		// IsContainer overrides.
		virtual bool IsContainer() const override { return m_valueIsFolder; }

		ibPredefinedValueObject(
			const ibGuid& predefinedGuid, const wxString& strPredefinedName,
			const wxString& strCode, const wxString& strDescription,
			bool valueIsFolder = false, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>())
			:
			m_predefinedItemGuid(predefinedGuid),
			m_strPredefinedName(strPredefinedName),
			m_strCode(strCode), m_strDescription(strDescription),
			m_valueIsFolder(valueIsFolder),
			m_valueParent(valueParent)
		{
		}

		ibGuid GetPredefinedGuid() const { return m_predefinedItemGuid; }

		wxString GetPredefinedParentName() const {
			if (m_valueParent != nullptr)
				return m_valueParent->GetPredefinedName();
			return wxT("");
		}

		wxString GetPredefinedName() const { return m_strPredefinedName; }
		wxString GetPredefinedCode() const { return m_strCode; }
		wxString GetPredefinedDescription() const { return m_strDescription; }

		bool IsPredefinedFolder() const { return m_valueIsFolder; }
		wxObjectDataPtr<ibPredefinedValueObject> GetPredefinedParent() const { return m_valueParent; }

		friend class ibValueMetaObjectRecordDataHierarchyMutableRef;

	private:

		ibGuid m_predefinedItemGuid;

		wxString m_strPredefinedName;
		wxString m_strCode;
		wxString m_strDescription;

		bool m_valueIsFolder;

		wxObjectDataPtr<ibPredefinedValueObject> m_valueParent;
	};

	//is predefined value?
	bool HasPredefinedValue(const ibGuid& valueGuid) const { return FindPredefinedValue(valueGuid) != nullptr; }
	bool HasPredefinedValue(const wxString& strPredefinedName) const { return FindPredefinedValue(strPredefinedName) != nullptr; }

	//append predefined value
	void AppendPredefinedValue(const wxString& strPredefinedName,
		const wxString& strCode, const wxString& strDescription,
		bool valueIsFolder = false, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>());

	void SetPredefinedValue(const ibGuid& predefinedGuid,
		const wxString& strPredefinedName,
		const wxString& strCode, const wxString& strDescription,
		bool valueIsFolder = false, const wxObjectDataPtr<ibPredefinedValueObject>& valueParent = wxObjectDataPtr<ibPredefinedValueObject>());

	void DeletePredefinedValue(const ibGuid& predefinedGuid);

	//find predefined value
	wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const ibGuid& predefinedGuid) const {

		auto iterator = std::find_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedGuid](const auto& value) { return predefinedGuid == value->GetPredefinedGuid(); });

		if (iterator != m_predefinedObjectVector.end())
			return *iterator;

		return wxObjectDataPtr<ibPredefinedValueObject>();
	}

	wxObjectDataPtr<ibPredefinedValueObject> FindPredefinedValue(const wxString& predefinedName) const {

		auto iterator = std::find_if(m_predefinedObjectVector.begin(), m_predefinedObjectVector.end(),
			[predefinedName](const auto& value) { return predefinedName == value->GetPredefinedName(); });

		if (iterator != m_predefinedObjectVector.end())
			return *iterator;

		return wxObjectDataPtr<ibPredefinedValueObject>();
	}

	//predefined values
	const std::vector<wxObjectDataPtr<ibPredefinedValueObject>>& GetPredefinedValueArray() const { return m_predefinedObjectVector; }

	// …as the server's editor holds them (ibProtocol::ibSchema::ibKind::Predefined), read anew — when the editor opens and after
	// each of its edits; the array keeps what was read till then.
	void ReadPredefinedValues();

private:
	std::vector<wxObjectDataPtr<ibPredefinedValueObject>> m_predefinedObjectVector;
};

#endif
