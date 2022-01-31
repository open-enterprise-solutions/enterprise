#ifndef _TABLE_MODEL_H__
#define _TABLE_MODEL_H__

#include "common/actionInfo.h"

#include "compiler/value.h"
#include "compiler/valueTypeDescription.h"

class Guid;
class IValueTable;
class CDataViewCtrl;

#include <wx/dataview.h>

class CTableModelNotifier {
	CDataViewCtrl *m_mainWindow;
public:
	CTableModelNotifier(CDataViewCtrl *mainWindow) { m_mainWindow = mainWindow; }

	wxDataViewItem GetSelection() const;
	int GetSelections(wxDataViewItemArray &sel) const;

	void StartEditing(const wxDataViewItem &item, unsigned int col) const;
};

class CDataViewCtrl : public wxDataViewCtrl {
	CTableModelNotifier *m_genNotitfier;

public:
	CDataViewCtrl() : wxDataViewCtrl()
	{
		m_genNotitfier = NULL;
	}

	CDataViewCtrl(wxWindow *parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(wxDataViewCtrlNameStr))
		: wxDataViewCtrl(parent, id, pos, size, style, validator, name)
	{
		m_genNotitfier = NULL;
	}

	virtual ~CDataViewCtrl()
	{
		wxDELETE(m_genNotitfier);
	}

	virtual bool AssociateModel(IValueTable *model);
	virtual void Select(const wxDataViewItem &item) override;
};

//Поддержка таблиц
class IValueTable : public CValue,
	public IActionSource, public wxDataViewModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:
	class IValueTableColumns : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueTableColumns);
	public:
		class IValueTableColumnsInfo : public CValue {
			wxDECLARE_ABSTRACT_CLASS(IValueTableColumnsInfo);
		public:

			virtual unsigned int GetColumnID() const = 0;
			virtual void SetColumnID(unsigned int col_id) {};
			virtual wxString GetColumnName() const = 0;
			virtual void SetColumnName(const wxString &name) {};
			virtual wxString GetColumnCaption() const = 0;
			virtual void SetColumnCaption(const wxString &caption) {};
			virtual CValueTypeDescription *GetColumnTypes() const = 0;
			virtual void SetColumnTypes(CValueTypeDescription *td) {};
			virtual int GetColumnWidth() const = 0;
			virtual void SetColumnWidth(int width) {};

			IValueTableColumnsInfo() : CValue(eValueTypes::TYPE_VALUE, true) {}
			virtual ~IValueTableColumnsInfo() {}
		};
	public:

		virtual IValueTableColumnsInfo *AddColumn(const wxString &colName, CValueTypeDescription *types, const wxString &caption, int width = wxDVC_DEFAULT_WIDTH) = 0;
		virtual void RemoveColumn(unsigned int col) = 0; 

		virtual bool HasColumnID(unsigned int col_id);
		virtual IValueTableColumnsInfo *GetColumnByID(unsigned int col_id);
		virtual IValueTableColumnsInfo *GetColumnByName(const wxString &colName);

		virtual IValueTableColumnsInfo *GetColumnInfo(unsigned int idx) const = 0;
		virtual unsigned int GetColumnCount() const = 0;

		IValueTableColumns() : CValue(eValueTypes::TYPE_VALUE, true) {}
		virtual ~IValueTableColumns() {}
	};

	class IValueTableReturnLine : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueTableReturnLine);
	public:

		virtual wxDataViewItem GetLineTableItem() const {
			return GetOwnerTable()->GetItem(GetLineTable());
		}

		virtual unsigned int GetLineTable() const = 0;
		virtual IValueTable *GetOwnerTable() const = 0;

		IValueTableReturnLine();
		virtual ~IValueTableReturnLine();

		//set meta/get meta
		virtual void SetValueByMetaID(meta_identifier_t id, const CValue &cVal) {}
		virtual CValue GetValueByMetaID(meta_identifier_t id) const { return CValue(); }

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue &cParam) const override
		{
			IValueTableReturnLine *tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine))
			{
				IValueTable *ownerTable = GetOwnerTable(); unsigned int lineTable = GetLineTable();
				wxASSERT(ownerTable);
				if (ownerTable == tableReturnLine->GetOwnerTable()
					&& lineTable == GetLineTable())
				{
					return true;
				}
			}
			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue &cParam) const override
		{
			IValueTableReturnLine *tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine))
			{
				IValueTable *ownerTable = GetOwnerTable(); unsigned int lineTable = GetLineTable();
				wxASSERT(ownerTable);
				if (ownerTable == tableReturnLine->GetOwnerTable()
					&& lineTable == GetLineTable())
				{
					return false;
				}
			}
			return true;
		}
	};

public:

	IValueTable(unsigned int initial_size = 0);
	virtual ~IValueTable() {}

	void AppendNotifier(CTableModelNotifier *notify) {
		if (m_srcNotifier) {
			wxDELETE(m_srcNotifier);
		}
		m_srcNotifier = notify;
	}

	void RemoveNotifier(CTableModelNotifier *notify) {
		if (m_srcNotifier == notify) {
			wxDELETE(m_srcNotifier);
		}
	}

	void RowPrepended();
	void RowInserted(unsigned int before);
	void RowAppended();
	void RowDeleted(unsigned int row);
	void RowsDeleted(const wxArrayInt &rows);
	void RowChanged(unsigned int row);
	void RowValueChanged(unsigned int row, unsigned int col);
	void Reset(unsigned int new_size);

	/////////////////////////////////////////////////////////

	wxDataViewItem GetSelection() const;

	int GetSelectionLine() const {
		wxDataViewItem selection = GetSelection();
		if (!selection.IsOk()) {
			return wxNOT_FOUND;
		}
		return GetRow(selection);
	}

	void RowValueStartEdit(const wxDataViewItem &item, unsigned int col = 0);

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class

	virtual void GetValueByRow(wxVariant &variant,
		unsigned int row, unsigned int col) const = 0;

	virtual bool SetValueByRow(const wxVariant &variant,
		unsigned int row, unsigned int col) = 0;

	virtual bool GetAttrByRow(unsigned int row, unsigned int col,
		wxDataViewItemAttr &attr) const
	{
		return false;
	}

	virtual bool IsEnabledByRow(unsigned int row,
		unsigned int col) const
	{
		return true;
	}

	virtual wxDataViewItem GetItem(unsigned int row) const;

	// helper methods provided by list models only
	virtual unsigned GetRow(const wxDataViewItem &item) const;

	// implement base methods
	virtual unsigned int GetChildren(const wxDataViewItem &item, wxDataViewItemArray &children) const override;

	// returns the number of rows
	virtual unsigned int GetCount() const { return (unsigned int)m_hash.GetCount(); }

	// implement some base class pure virtual directly
	virtual wxDataViewItem GetParent(const wxDataViewItem & item) const override
	{
		// items never have valid parent in this model
		return wxDataViewItem();
	}

	virtual bool IsContainer(const wxDataViewItem &item) const override
	{
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const override
	{
		GetValueByRow(variant, GetRow(item), col);
	}

	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) override
	{
		return SetValueByRow(variant, GetRow(item), col);
	}

	virtual bool GetAttr(const wxDataViewItem &item, unsigned int col,
		wxDataViewItemAttr &attr) const override
	{
		return GetAttrByRow(GetRow(item), col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem &item, unsigned int col) const override
	{
		return IsEnabledByRow(GetRow(item), col);
	}

	virtual bool IsListModel() const override { return true; }

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool AutoCreateColumns() { return true; }
	virtual bool EditableLine(const wxDataViewItem &item, unsigned int col) { return true; }

	virtual IValueTableReturnLine *GetRowAt(wxDataViewItem &item)
	{
		return GetRowAt(GetRow(item));
	}

	virtual void ActivateItem(CValueForm *formOwner,
		const wxDataViewItem &item, unsigned int col) {
		IValueTable::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0) {}
	virtual void CopyValue() {}
	virtual void EditValue() {}
	virtual void DeleteValue() {}

	virtual wxDataViewItem GetLineByGuid(const Guid &guid) const { return wxDataViewItem(0); }

	virtual IValueTableReturnLine *GetRowAt(unsigned int line) = 0;
	virtual IValueTableColumns *GetColumns() const = 0;

	//ref counter 
	virtual void DecrRef() override
	{
		if (CValue::GetRefCount() > 1) {
			CValue::DecrRef();
		}
		else if (wxRefCounter::GetRefCount() < 2) {
			CValue::DecrRef();
		}
	}

	/**
	* Override actions
	*/

	virtual actionData_t GetActions(form_identifier_t formType);
	virtual void ExecuteAction(action_identifier_t action, class CValueForm *srcForm);

private:

	CTableModelNotifier *m_srcNotifier;

	wxDataViewItemArray m_hash;
	unsigned int m_nextFreeID;
	bool m_ordered;
};

#endif 