#ifndef __GENERIC_DATA_H__
#define __GENERIC_DATA_H__

#include "backend/backend.h"                              // BACKEND_API (base header, not includer-order dependent)
#include "backend/uniqueKey.h"                            // ibUniqueKey (form guid)
#include "backend/standardCommand.h"                           // ibCommandItem / ibFormID / ibActionID — the metaobject's command contract
#include "backend/metaCollection/metaObjectComposite.h"   // ibValueMetaObjectCompositeData (base) + FillArrayObjectByFilter templates
#include "backend/metaCollection/metaFormObject.h"        // ibValueMetaObjectFormBase + ibBackendCommandItem (bases), ibBackendValueForm, defaultFormType (ibSelectorDataType comes transitively via backend_type.h)
#include "backend/metaCollection/metaCommandObject.h"      // ibValueMetaObjectCommand — GetCommandArrayObject returns the real command type
#include "backend/metaCollection/metaSpreadsheetObject.h" // ibValueMetaObjectSpreadsheetBase

class BACKEND_API ibSourceDataObject;
class BACKEND_API ibBackendControlFrame;
class BACKEND_API ibValueManagerDataObject;
class BACKEND_API ibValueSpreadsheetDocument;

class BACKEND_API ibFormTypeList {

	struct ibFormTypeItem {
		bool m_isOk;
		wxString m_strName;
		wxString m_strLabel;
		wxString m_strHelp;
		long m_id;
	public:

		ibFormTypeItem() :
			m_isOk(true), m_strName(), m_strLabel(), m_id(-1)
		{
		}

		ibFormTypeItem(const wxString& name, const long& l) :
			m_isOk(true), m_strName(name), m_strLabel(name), m_id(l)
		{
		}

		ibFormTypeItem(const wxString& name, const wxString& label, const long& l) :
			m_isOk(true), m_strName(name), m_strLabel(label), m_id(l)
		{
		}

		ibFormTypeItem(const wxString& name, const wxString& label, const wxString& help, const long& l) :
			m_isOk(true), m_strName(name), m_strLabel(label), m_strHelp(help), m_id(l)
		{
		}

		ibFormTypeItem(const ibFormTypeItem& item) :
			m_isOk(true), m_strName(item.m_strName), m_strLabel(item.m_strLabel), m_strHelp(item.m_strHelp), m_id(item.m_id)
		{
		}

		ibFormTypeItem& operator = (const ibFormTypeItem& src) {
			m_strName = src.m_strName;
			m_strLabel = src.m_strLabel;
			m_strHelp = src.m_strHelp;
			m_id = src.m_id;
			return *this;
		}

		operator const long() const { return m_id; }
	};

	ibFormTypeItem GetItemAt(const unsigned int idx) const {
		if (idx >= m_listTypeForm.size())
			return ibFormTypeItem();
		auto it = m_listTypeForm.begin();
		std::advance(it, idx);
		return *it;
	};

public:

	void ResetListItem() { m_listTypeForm.clear(); }

	void AppendItem(const wxString& name, const int& l) { (void)m_listTypeForm.emplace_back(name, l); }
	void AppendItem(const wxString& name, const wxString& label, const int& l) { (void)m_listTypeForm.emplace_back(name, label, l); }
	void AppendItem(const wxString& name, const wxString& label, const wxString& help, const int& l) { (void)m_listTypeForm.emplace_back(label, help, l); }

	wxString GetItemName(const unsigned int idx) const { return GetItemAt(idx).m_strName; }
	wxString GetItemLabel(const unsigned int idx) const { return GetItemAt(idx).m_strLabel; }
	wxString GetItemHelp(const unsigned int idx) const { return GetItemAt(idx).m_strHelp; }
	long GetItemId(const unsigned int idx) const { return GetItemAt(idx).m_id; }

	unsigned int GetItemCount() const { return (unsigned int)m_listTypeForm.size(); }

private:
	std::vector<ibFormTypeItem> m_listTypeForm;
};

class BACKEND_API ibValueMetaObjectGenericData
	: public ibValueMetaObjectCompositeData, public ibBackendCommandItem {
	public:
	friend class ibMetaData;
public:

#pragma region access_generic
	virtual bool AccessRight_Show() const { return true; }
	// MODIFY right — the GENERIC "can change this" predicate, the write-side twin of AccessRight_Show (which
	// generalises "can view": a common form maps it to Use, a catalog to Read). Each object maps Modify to its
	// own concrete right — a record / register / constant to its Write role — so a read-only role denies it.
	// Virtual on the base so a form reads it polymorphically off GetSourceMetaObject() (view-only mode) without
	// knowing the concrete metaobject. Default true: a metaobject with no modify concept (data processor /
	// report) is never view-only-gated.
	virtual bool AccessRight_Modify() const { return true; }
#pragma endregion

	// (Source list COMMANDS live as plain virtuals on the record / register / constant metaobjects, forwarded by the
	//  templated source descriptor — NOT here: GenericData carries no such surface, so nothing is pushed onto it.)

	// value(<Kind>.<Name>.<Member>) — the L4-1 literal-reference constant (1C ЗНАЧЕНИЕ): resolve a metaobject's
	// EmptyRef or one of its predefined items to a runtime ibValue. Reached off GetSourceMetaObject() at lowering
	// (the queryable already vends the metaobject — nothing is added to the queryable). Returns TRUE + the value in
	// `out` when the member resolves; FALSE otherwise (the query engine raises the exception, it owns the source
	// span). The GENERIC base has no constants → false; the record level resolves the empty reference, the
	// hierarchy level adds predefined items.
	virtual bool ResolveQueryConstant(const wxString& member, ibValue& out) const;

	virtual ibClassID ResolveChild(const ibClassID& clsid) const {
		if (clsid == g_metaFormCLSID ||
			clsid == g_metaTemplateCLSID ||
			clsid == g_metaCommandCLSID)   // every business object owns its own commands (object scope)
			return clsid;
		return 0;
	}

	//get data selector 
	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_reference;
	}

#pragma region __array_h__

	//form
	std::vector<ibValueMetaObjectFormBase*> GetFormArrayObject(
		std::vector<ibValueMetaObjectFormBase*> array = std::vector<ibValueMetaObjectFormBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectFormBase>(array, { g_metaFormCLSID });
		return array;
	}

	//commands (object scope) — a business object owns its own commands; returns the REAL command type (like
	// GetFormArrayObject returns FormBase*), the clsid filter makes the finder's static_cast type-safe.
	std::vector<ibValueMetaObjectCommand*> GetCommandArrayObject(
		std::vector<ibValueMetaObjectCommand*> array = std::vector<ibValueMetaObjectCommand*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectCommand>(array, { g_metaCommandCLSID });
		return array;
	}

	//grid
	std::vector<ibValueMetaObjectSpreadsheetBase*> GetTemplateArrayObject(
		std::vector<ibValueMetaObjectSpreadsheetBase*> array = std::vector<ibValueMetaObjectSpreadsheetBase*>()) const {
		FillArrayObjectByFilter<ibValueMetaObjectSpreadsheetBase>(array, { g_metaTemplateCLSID });
		return array;
	}

#pragma endregion
#pragma region __filter_h__

	//form
	template <typename _T1>
	ibValueMetaObjectFormBase* FindFormObjectByFilter(const _T1& id, const ibFormID& form_id = wxNOT_FOUND) const {
		ibValueMetaObjectFormBase* founded = FindObjectByFilter<ibValueMetaObjectFormBase>(id, { g_metaCommonFormCLSID, g_metaFormCLSID });
		if ((founded != nullptr && form_id == founded->GetTypeForm()) || form_id == wxNOT_FOUND)
			return founded;
		return nullptr;
	}

	//grid
	template <typename _T1>
	ibValueMetaObjectSpreadsheetBase* FindTemplateObjectByFilter(const _T1& id) const {
		return FindObjectByFilter<ibValueMetaObjectSpreadsheetBase>(id, { g_metaCommonTemplateCLSID, g_metaTemplateCLSID });
	}

#pragma endregion 

	//form events 
	virtual void OnCreateFormObject(ibValueMetaObjectFormBase* metaForm) {}
	virtual void OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm) {}

	//Get form type
	virtual ibFormTypeList GetFormType() const = 0;

	//Get metaObject by def id
	virtual ibValueMetaObjectFormBase* GetDefaultFormByID(const ibFormID& id) const { return nullptr; }

	// Build the form value of one of MY form metaobjects, bound to the source its kind implies
	// (a list form gets the list, an object form a NEW object).
	//
	// `formGuid` is the FORM KEY. It defaults to EMPTY — the runtime meaning — so a plain
	// CreateObjectForm(metaForm) does the safe thing: the key falls back to the SOURCE
	// object's guid, which is how everything finds a live form afterwards
	// (ibValueRecordDataObject::GetForm / Modify / the write notify all call
	// FindFormByUniqueKey(m_objGuid)). Only the DESIGNER's compile cache passes a guid — the
	// METAFORM's — because its value IS one per metaform and is keyed that way. Keyed by the
	// metaform, a runtime form would be invisible to the lookups above and Save / Refresh
	// would have nothing to act on.
	virtual ibBackendValueForm* CreateObjectForm(const ibValueMetaObjectFormBase* metaForm,
		const ibUniqueKey& formGuid = wxNullGuid) const {
		return ibValueMetaObjectGenericData::CreateAndBuildForm(
			metaForm != nullptr ? metaForm->GetName() : wxString(wxEmptyString),
			metaForm != nullptr ? metaForm->GetTypeForm() : defaultFormType,
			nullptr,
			CreateSourceObject(metaForm),
			formGuid
		);
	}

#pragma region _form_builder_h_
	//support form 
	ibBackendValueForm* GetGenericForm(const wxString& strFormName = wxEmptyString,
		ibBackendControlFrame* ownerControl = nullptr, const ibUniqueKey& formGuid = wxNullGuid) const;
#pragma endregion

#pragma region _form_creator_h_
	ibBackendValueForm* CreateAndBuildForm(const wxString& strFormName, const ibFormID& form_id = defaultFormType,
		ibBackendControlFrame* ownerControl = nullptr,
		ibSourceDataObject* srcObject = nullptr,
		const ibUniqueKey& formGuid = wxNullGuid
	) const;
#pragma endregion

#pragma region _template_builder_h_

	class ibValueSpreadsheetDocument* GetTemplate(const wxString& strFormName) const;

#pragma endregion

	virtual ibValueManagerDataObject* CreateManagerDataObjectValue() const = 0;

protected:

	//create object data with meta form
	virtual ibSourceDataObject* CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const { return nullptr; }
};

#endif
