#ifndef _TABLE_MODEL_H__
#define _TABLE_MODEL_H__

#include "common/actionInfo.h"

#include "compiler/value.h"
#include "compiler/valueTypeDescription.h"

class CDataViewCtrl;

#include <wx/dataview.h>

class CTableModelNotifier {
	CDataViewCtrl* m_mainWindow;
public:
	CTableModelNotifier(CDataViewCtrl* mainWindow) {
		m_mainWindow = mainWindow;
	}

	void Select(const wxDataViewItem& sel) const;
	wxDataViewItem GetSelection() const;
	int GetSelections(wxDataViewItemArray& sel) const;

	void StartEditing(const wxDataViewItem& item, unsigned int col) const;
};

class Guid;

//Общая сущность для таблиц и древа таблиц 
class IValueModel : public CValue,
	public IActionSource, public wxDataViewModel {
	wxDECLARE_ABSTRACT_CLASS(IValueModel);
public:

	class IValueTableColumnCollection : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueTableColumnCollection);
	public:
		class IValueTableColumnInfo : public CValue {
			wxDECLARE_ABSTRACT_CLASS(IValueTableColumnInfo);
		public:

			virtual unsigned int GetColumnID() const = 0;
			virtual void SetColumnID(unsigned int col_id) {};
			virtual wxString GetColumnName() const = 0;
			virtual void SetColumnName(const wxString& name) {};
			virtual wxString GetColumnCaption() const = 0;
			virtual void SetColumnCaption(const wxString& caption) {};
			virtual CValueTypeDescription* GetColumnTypes() const = 0;
			virtual void SetColumnTypes(CValueTypeDescription* td) {};
			virtual int GetColumnWidth() const = 0;
			virtual void SetColumnWidth(int width) {};

			IValueTableColumnInfo();
			virtual ~IValueTableColumnInfo();

			virtual CMethods* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
				PrepareNames(); return m_methods;
			}
			virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов

			virtual CValue GetAttribute(attributeArg_t& aParams); //значение атрибута

		protected:
			CMethods* m_methods;
		};
	public:

		virtual IValueTableColumnInfo* AddColumn(const wxString& colName,
			CValueTypeDescription* types,
			const wxString& caption,
			int width = wxDVC_DEFAULT_WIDTH) {
			return NULL;
		};

		virtual void RemoveColumn(unsigned int col) {};

		virtual bool HasColumnID(unsigned int col_id);
		virtual IValueTableColumnInfo* GetColumnByID(unsigned int col_id);
		virtual IValueTableColumnInfo* GetColumnByName(const wxString& colName);

		virtual IValueTableColumnInfo* GetColumnInfo(unsigned int idx) const = 0;
		virtual unsigned int GetColumnCount() const = 0;

		IValueTableColumnCollection() : CValue(eValueTypes::TYPE_VALUE, true) {}
		virtual ~IValueTableColumnCollection() {}
	};

	class IValueTableReturnLine : public CValue {
		wxDECLARE_ABSTRACT_CLASS(IValueTableReturnLine);
	public:

		virtual wxDataViewItem GetLineTableItem() const {
			return GetOwnerTable()->GetItem(GetLineTable());
		}

		virtual unsigned int GetLineTable() const = 0;
		virtual IValueModel* GetOwnerTable() const = 0;

		IValueTableReturnLine();
		virtual ~IValueTableReturnLine();

		//set meta/get meta
		virtual void SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal) {}
		virtual CValue GetValueByMetaID(const meta_identifier_t& id) const { return CValue(); }

		//operator '=='
		virtual inline bool CompareValueEQ(const CValue& cParam) const override {
			IValueTableReturnLine* tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine)) {
				IValueModel* ownerTable = GetOwnerTable(); unsigned int lineTable = GetLineTable();
				wxASSERT(ownerTable);
				if (ownerTable == tableReturnLine->GetOwnerTable()
					&& lineTable == GetLineTable()) {
					return true;
				}
			}
			return false;
		}

		//operator '!='
		virtual inline bool CompareValueNE(const CValue& cParam) const override {
			IValueTableReturnLine* tableReturnLine = NULL;
			if (cParam.ConvertToValue(tableReturnLine)) {
				IValueModel* ownerTable = GetOwnerTable(); unsigned int lineTable = GetLineTable();
				wxASSERT(ownerTable);
				if (ownerTable == tableReturnLine->GetOwnerTable()
					&& lineTable == GetLineTable()) {
					return false;
				}
			}
			return true;
		}
	};

public:

	IValueModel();
	virtual ~IValueModel() {}

	void AppendNotifier(CTableModelNotifier* notify) {
		if (m_srcNotifier != NULL) {
			wxDELETE(m_srcNotifier);
		}
		m_srcNotifier = notify;
	}

	void RemoveNotifier(CTableModelNotifier* notify) {
		if (m_srcNotifier == notify) {
			wxDELETE(m_srcNotifier);
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool IsEmpty() const override {
		return GetCount() == 0;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual wxDataViewItem GetSelection() const;
	virtual void RowValueStartEdit(const wxDataViewItem& item, unsigned int col = 0);

	virtual wxDataViewItem GetItem(unsigned int row) const = 0;

	// helper methods provided by list models only
	virtual unsigned GetRow(const wxDataViewItem& item) const = 0;

	// returns the number of rows
	virtual unsigned int GetCount() const = 0;

	//////////////////////////////////////////////////////////////////////////////////////////////////

	virtual bool AutoCreateColumns() const { return true; }

	virtual bool EditableLine(const wxDataViewItem& item, unsigned int col) const { return true; }

	virtual IValueTableReturnLine* GetRowAt(wxDataViewItem& item) {
		return GetRowAt(GetRow(item));
	}

	virtual void ActivateItem(CValueForm* formOwner,
		const wxDataViewItem& item, unsigned int col) {
		IValueModel::RowValueStartEdit(item, col);
	}

	virtual void AddValue(unsigned int before = 0) = 0;
	virtual void CopyValue() = 0;
	virtual void EditValue() = 0;
	virtual void DeleteValue() = 0;

	virtual wxDataViewItem GetLineByGuid(const Guid& guid) const {
		return wxDataViewItem(NULL);
	}

	virtual IValueTableReturnLine* GetRowAt(unsigned int line) = 0;
	virtual IValueTableColumnCollection* GetColumns() const = 0;

	//ref counter 
	virtual void DecrRef() override {
		if (CValue::GetRefCount() > 1) {
			CValue::DecrRef();
		}
		else if (wxRefCounter::GetRefCount() < 2) {
			CValue::DecrRef();
		}
	}

	//Update model 
	virtual void RefreshModel() {};

	/**
	* Override actions
	*/

	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, class CValueForm* srcForm);

private:
	CTableModelNotifier* m_srcNotifier;
};

//Поддержка таблиц
class IValueTable : public IValueModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:

	IValueTable(unsigned int initial_size = 0) : IValueModel() {
		// IDs are ordered until an item gets deleted or inserted
		m_ordered = true;
		// build initial index
		unsigned int i;
		for (i = 1; i < initial_size + 1; i++)
			m_hash.Add(wxDataViewItem(wxUIntToPtr(i)));
		m_nextFreeID = initial_size + 1;
	}

	virtual ~IValueTable() {}

	/////////////////////////////////////////////////////////

	void RowPrepended() {
		m_ordered = false;
		unsigned int id = m_nextFreeID;
		m_nextFreeID++;
		wxDataViewItem item(wxUIntToPtr(id));
		m_hash.Insert(item, 0);
		/* wxDataViewModel:: */ ItemAdded(wxDataViewItem(NULL), item);
	}

	void RowInserted(unsigned int before) {
		m_ordered = false;
		unsigned int id = m_nextFreeID;
		m_nextFreeID++;
		wxDataViewItem item(wxUIntToPtr(id));
		m_hash.Insert(item, before);
		/* wxDataViewModel:: */ ItemAdded(wxDataViewItem(NULL), item);
	}

	void RowAppended() {
		unsigned int id = m_nextFreeID;
		m_nextFreeID++;
		wxDataViewItem item(wxUIntToPtr(id));
		m_hash.Add(item);
		ItemAdded(wxDataViewItem(NULL), item);
	}

	void RowDeleted(unsigned int row) {
		m_ordered = false;
		wxDataViewItem item(m_hash[row]);
		m_hash.RemoveAt(row);
	}

	void RowsDeleted(const wxArrayInt& rows) {
		m_ordered = false;
		wxDataViewItemArray array;
		unsigned int i;
		for (i = 0; i < rows.GetCount(); i++)
		{
			wxDataViewItem item(m_hash[rows[i]]);
			array.Add(item);
		}
		wxArrayInt sorted = rows;
		sorted.Sort([](int* v1, int* v2) {
			return *v2 - *v1;
			}
		);
		for (i = 0; i < sorted.GetCount(); i++)
			m_hash.RemoveAt(sorted[i]);
		/* wxDataViewModel:: */ ItemsDeleted(wxDataViewItem(0), array);
	}

	void RowChanged(unsigned int row) {
		/* wxDataViewModel:: */ ItemChanged(GetItem(row));
	}

	void RowValueChanged(unsigned int row, unsigned int col) {
		/* wxDataViewModel:: */ ValueChanged(GetItem(row), col);
	}

	void Reset(unsigned int new_size) {
		/* wxDataViewModel:: */ BeforeReset();
		m_hash.Clear();
		// IDs are ordered until an item gets deleted or inserted
		m_ordered = true;
		// build initial index
		unsigned int i;
		for (i = 1; i < new_size + 1; i++)
			m_hash.Add(wxDataViewItem(wxUIntToPtr(i)));
		m_nextFreeID = new_size + 1;
		/* wxDataViewModel:: */ AfterReset();
	}

	/////////////////////////////////////////////////////////

	int GetSelectionLine() const {
		const wxDataViewItem& selection = GetSelection();
		if (!selection.IsOk())
			return wxNOT_FOUND;
		return GetRow(selection);
	}

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		unsigned row, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		unsigned row, unsigned col) = 0;

	virtual bool GetAttrByRow(unsigned WXUNUSED(row), unsigned WXUNUSED(col),
			wxDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(unsigned int WXUNUSED(row),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

	// helper methods provided by list models only
	virtual unsigned GetRow(const wxDataViewItem& item) const {
		if (m_ordered)
			return wxPtrToUInt(item.GetID()) - 1;
		// assert for not found
		return (unsigned int)m_hash.Index(item);
	}

	virtual wxDataViewItem GetItem(unsigned int row) const {
		wxASSERT(row < m_hash.GetCount());
		return wxDataViewItem(m_hash[row]);
	}

	// implement base methods
	virtual unsigned int GetChildren(const wxDataViewItem& item, wxDataViewItemArray& children) const override {
		if (item.IsOk())
			return 0;
		children = m_hash;
		return m_hash.GetCount();
	}

	// returns the number of rows
	virtual unsigned int GetCount() const override { 
		return (unsigned int)m_hash.GetCount(); 
	}

	// implement some base class pure virtual directly
	virtual wxDataViewItem GetParent(const wxDataViewItem& WXUNUSED(item)) const override {
		// items never have valid parent in this model
		return wxDataViewItem();
	}

	virtual bool IsContainer(const wxDataViewItem& item) const override {
		// only the invisible (and invalid) root item has children
		return !item.IsOk();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, GetRow(item), col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, GetRow(item), col);
	}

	virtual bool GetAttr(const wxDataViewItem& item, unsigned int col,
		wxDataViewItemAttr& attr) const override {
		return GetAttrByRow(GetRow(item), col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(GetRow(item), col);
	}

private:
	wxDataViewItemArray m_hash;
	unsigned int m_nextFreeID;
	bool m_ordered;
};

//Поддержка дерева таблиц 
class IValueTree : public IValueModel {
	wxDECLARE_ABSTRACT_CLASS(IValueTable);
public:

	struct wxValueTableNode {
	
		wxValueTableNode(wxValueTableNode* parent) :
			m_parent(parent) {
		}

		~wxValueTableNode() {
			// free all our children nodes
			size_t count = m_children.size();
			for (size_t i = 0; i < count; i++) {
				wxValueTableNode* child = m_children[i];
				delete child;
			}
		}

		bool IsContainer() const {
			return m_children.size() > 0;
		}

		wxValueTableNode* GetParent() const {
			return m_parent;
		}

		std::vector<wxValueTableNode*>& GetChildren() {
			return m_children;
		}

		wxValueTableNode* GetChild(unsigned int n) const {
			return m_children.at(n);
		}

		void Insert(wxValueTableNode* child, unsigned int n) {
			m_children.insert(m_children.begin() + n, child);
		}

		void Append(wxValueTableNode* child) {
			m_children.push_back(child);
		}

		unsigned int GetChildCount() const {
			return m_children.size();
		}

	public:     // public to avoid getters/setters

	private:
		wxValueTableNode* m_parent;
		std::vector<wxValueTableNode*> m_children;
	};

public:

	IValueTree() : IValueModel() {
		m_root = new wxValueTableNode(NULL);
	}

	virtual ~IValueTree() {
		delete m_root;
	}

	/////////////////////////////////////////////////////////

	// helper methods to change the model
	void Delete(const wxDataViewItem& item) {
		wxValueTableNode* node = (wxValueTableNode*)item.GetID();
		if (!node)      // happens if item.IsOk()==false
			return;
		wxDataViewItem parent(node->GetParent());
		if (!parent.IsOk()) {
			wxASSERT(node == m_root);
			// don't make the control completely empty:
			wxLogError("Cannot remove the root item!");
			return;
		}
		// first remove the node from the parent's array of children;
		// NOTE: MyMusicTreeModelNodePtrArray is only an array of _pointers_
		//       thus removing the node from it doesn't result in freeing it
		std::vector<wxValueTableNode*>&children = node->GetParent()->GetChildren();	
		std::vector<wxValueTableNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);	
		if (children_iterator != children.end())
			children.erase(children_iterator);
		// free the node
		delete node;
		// notify control
		/* wxDataViewModel:: */ ItemDeleted(parent, item);
	}

	void Clear() {
		std::vector<wxValueTableNode*>& children = m_root->GetChildren();
		while (!children.empty()) {
			wxValueTableNode* node = m_root->GetChild(0);
			std::vector<wxValueTableNode*>::iterator children_iterator = std::find(children.begin(), children.end(), node);		
			if (children_iterator != children.end())
				children.erase(children_iterator);
			delete node;
		}

		/* wxDataViewModel:: */ Cleared();
	}

	/////////////////////////////////////////////////////////

	int GetSelectionLine() const {
		const wxDataViewItem &selection = GetSelection();
		if (!selection.IsOk())
			return wxNOT_FOUND;
		return GetRow(selection);
	}

	// override sorting to always sort branches ascendingly
	virtual int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
		unsigned int column, bool ascending) const override {
		wxASSERT(item1.IsOk() && item2.IsOk());
		// should never happen
		if (IsContainer(item1) && IsContainer(item2)) {
			wxVariant value1, value2;
			GetValue(value1, item1, 0);
			GetValue(value2, item2, 0);
			wxString str1 = value1.GetString();
			wxString str2 = value2.GetString();
			int res = str1.Cmp(str2);
			if (res)
				return res;
			// items must be different
			wxUIntPtr litem1 = (wxUIntPtr)item1.GetID();
			wxUIntPtr litem2 = (wxUIntPtr)item2.GetID();
			return litem1 - litem2;
		}

		return wxDataViewModel::Compare(item1, item2, column, ascending);
	}

	// derived classes should override these methods instead of
	// {Get,Set}Value() and GetAttr() inherited from the base class
	virtual void GetValueByRow(wxVariant& variant,
		const wxDataViewItem& item, unsigned col) const = 0;

	virtual bool SetValueByRow(const wxVariant& variant,
		const wxDataViewItem& item, unsigned col) = 0;

	virtual bool GetAttrByRow(const wxDataViewItem& WXUNUSED(item), 
		unsigned WXUNUSED(col), wxDataViewItemAttr& WXUNUSED(attr)) const {
		return false;
	}

	virtual bool IsEnabledByRow(const wxDataViewItem& WXUNUSED(item),
		unsigned int WXUNUSED(col)) const {
		return true;
	}

	virtual unsigned int GetChildren(const wxDataViewItem& parent,
		wxDataViewItemArray& array) const override {
		wxValueTableNode* node = (wxValueTableNode*)parent.GetID();
		if (node == NULL) {
			array.Add(wxDataViewItem((void*)m_root));
			return 1;
		}
		unsigned int count = node->GetChildCount();
		if (count == 0)
			return 0;
		for (unsigned int pos = 0; pos < count; pos++) {
			wxValueTableNode* child = node->GetChildren().at(pos);
			array.Add(wxDataViewItem((void*)child));
		}
		return count;
	}

	// returns the number of rows
	virtual unsigned int GetCount() const override {
		return (unsigned int)m_root->GetChildCount();
	}

	virtual wxDataViewItem GetParent(const wxDataViewItem& item) const override {
		// the invisible root node has no parent
		if (!item.IsOk())
			return wxDataViewItem(NULL);
		wxValueTableNode* node = (wxValueTableNode*)item.GetID();
		// "root" also has no parent
		if (node == m_root)
			return wxDataViewItem(NULL);
		return wxDataViewItem((void*)node->GetParent());
	}

	virtual bool IsContainer(const wxDataViewItem& item) const override {
		// the invisible root node can have children
		// (in our model always "root")
		if (!item.IsOk())
			return true;
		wxValueTableNode* node = (wxValueTableNode*)item.GetID();
		return node->IsContainer();
	}

	// and implement some others by forwarding them to our own ones
	virtual void GetValue(wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) const override {
		GetValueByRow(variant, item, col);
	}

	virtual bool SetValue(const wxVariant& variant,
		const wxDataViewItem& item, unsigned int col) override {
		return SetValueByRow(variant, item, col);
	}

	virtual bool GetAttr(const wxDataViewItem& item, unsigned int col,
		wxDataViewItemAttr& attr) const override {
		return GetAttrByRow(item, col, attr);
	}

	virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override {
		return IsEnabledByRow(item, col);
	}

private:
	wxValueTableNode* m_root;
};

#endif 