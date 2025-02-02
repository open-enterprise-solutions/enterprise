#ifndef  __META_TREE_WRAPPER_H__
#define  __META_TREE_WRAPPER_H__

#include "backend_form.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////
class IMetaObject;
/////////////////////////////////////////////////////////////////////////////////////////////////////

class BACKEND_API IBackendMetadataTree {
public:

	virtual form_identifier_t SelectFormType(class CMetaObjectForm* metaObject) const = 0;

	virtual void SetReadOnly(bool readOnly = true) = 0;
	virtual void Modify(bool modify) = 0;

	virtual void EditModule(const wxString& fullName, int lineNumber, bool setRunLine = true) = 0;

	virtual bool OpenFormMDI(IMetaObject* obj) = 0;
	virtual bool OpenFormMDI(IMetaObject* obj, IBackendMetaDocument*& foundedDoc) = 0;
	virtual bool CloseFormMDI(IMetaObject* obj) = 0;

	virtual IBackendMetaDocument* GetDocument(IMetaObject* obj) const = 0;

	virtual bool RenameMetaObject(IMetaObject* obj, const wxString& sNewName) = 0;

	virtual void CloseMetaObject(IMetaObject* obj) = 0;
	virtual void OnCloseDocument(IBackendMetaDocument* doc) = 0;

	virtual void UpdateChoiceSelection() {}

protected:

	struct treeData_t {
		bool m_expanded = false;
	};

	struct treeClassIdentifierData_t : treeData_t {
		class_identifier_t m_clsid; //тип элемента
	public:
		treeClassIdentifierData_t(const class_identifier_t& clsid) :
			m_clsid(clsid) {
		}
	};

	struct treeMetaData_t : treeData_t {
		IMetaObject* m_metaObject; //тип элемента
	public:
		treeMetaData_t(IMetaObject* metaObject) :
			m_metaObject(metaObject) {
		}
	};
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
class IMetaDataConfiguration;
/////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // ! _META_TREE_
