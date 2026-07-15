#ifndef  __META_TREE_WRAPPER_H__
#define  __META_TREE_WRAPPER_H__

#include "backend_form.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibValueMetaObject;
/////////////////////////////////////////////////////////////////////////////////////////////////////

// Designer-side metadata tree facade. Compile-value cache moved to
// ibCompileValueCache (in metadata.h) — owned by ibMetaData when the
// configuration supports designer editing. Tree is now strictly UI-side.
class BACKEND_API ibBackendMetadataTree {
public:

	virtual ~ibBackendMetadataTree() {}

	virtual ibFormID SelectFormType(class ibValueMetaObjectForm* metaObject) const = 0;
	virtual void Activate() = 0;

	virtual void SetReadOnly(bool readOnly = true) = 0;
	virtual bool IsEditable() const = 0;

	virtual void Modify(bool modify) = 0;
	virtual void EditModule(const ibGuid& moduleName, int lineNumber, bool setRunLine = true) = 0;

	virtual bool OpenObjectForm(ibValueMetaObject* obj) = 0;
	virtual bool OpenObjectForm(ibValueMetaObject* obj, ibBackendMetaDocument*& foundedDoc) = 0;
	virtual bool CloseObjectForm(ibValueMetaObject* obj) = 0;

#pragma region __predefined_values_h__
	virtual void EditPredefinedValues(class ibValueMetaObjectRecordDataHierarchyMutableRef* obj) = 0;
#pragma endregion

	virtual ibBackendMetaDocument* GetDocument(ibValueMetaObject* obj) const = 0;

	virtual bool RenameMetaObject(ibValueMetaObject* obj, const wxString& strNewName) = 0;
	virtual void CloseMetaObject(ibValueMetaObject* obj) = 0;

	virtual void UpdateChoiceSelection() {}

protected:

	struct ibTreeData {
		bool m_expanded = false;
	};

	struct ibTreeDataClassIdentifier : ibTreeData {
		ibClassID m_clsid; // element type
	public:
		ibTreeDataClassIdentifier(const ibClassID& clsid) :
			m_clsid(clsid) {
		}
	};

	struct ibTreeDataMetaItem : ibTreeData {
		ibValueMetaObject* m_metaObject; // element type
	public:
		ibTreeDataMetaItem(ibValueMetaObject* metaObject) :
			m_metaObject(metaObject) {
		}
	};
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
class BACKEND_API ibMetaDataConfiguration;
/////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // ! _META_TREE_
