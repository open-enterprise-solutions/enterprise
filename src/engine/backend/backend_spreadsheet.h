#ifndef __BACKEND_CELL_H__
#define __BACKEND_CELL_H__

#include "spreadsheetDescription.h"

class BACKEND_API ibBackendSpreadsheetNotifier {
public:

	virtual ~ibBackendSpreadsheetNotifier() {}
	virtual void ClearSpreadsheet() = 0;
	virtual void EnableEditing(bool editable) = 0;

	//size 
	virtual void SetRowSize(int row, int height = 0) = 0;
	virtual void SetColSize(int col, int width = 0) = 0;

	//freeze 
	virtual void SetRowFreeze(int row) = 0;
	virtual void SetColFreeze(int col) = 0;

	// ------ row and col formatting
	//

	virtual void SetCellBackgroundColour(int row, int col, const wxColour& colour) = 0;
	virtual void SetCellTextColour(int row, int col, const wxColour& colour) = 0;
	virtual void SetCellTextOrient(int row, int col, const int orient) = 0;
	virtual void SetCellFont(int row, int col, const wxFont& font) = 0;
	virtual void SetCellAlignment(int row, int col, const int horiz, const int vert) = 0;
	virtual void SetCellBorderLeft(int row, int col, const ibSpreadsheetBorderDescription& desc) = 0;
	virtual void SetCellBorderRight(int row, int col, const ibSpreadsheetBorderDescription& desc) = 0;
	virtual void SetCellBorderTop(int row, int col, const ibSpreadsheetBorderDescription& desc) = 0;
	virtual void SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc) = 0;
	virtual void SetCellSize(int row, int col, int num_rows, int num_cols) = 0;
	virtual void SetCellFitMode(int row, int col, ibSpreadsheetCellDescription::ibFitMode fitMode) = 0;
	virtual void SetCellReadOnly(int row, int col, bool isReadOnly = true) = 0;

	// ------ cell brake accessors
	//
	//support printing 
	virtual void AddRowBrake(int row) = 0;
	virtual void AddColBrake(int col) = 0;

	virtual void DeleteRowBrake(int row) = 0;
	virtual void DeleteColBrake(int col) = 0;

	virtual void SetRowBrake(int row) = 0;
	virtual void SetColBrake(int col) = 0;

	// ------ cell value accessors
	//
	virtual void SetCellValue(int row, int col, const wxString& s) = 0;

	// ------ area value accessors
	//
	virtual void PutArea(
		const wxObjectDataPtr<class ibBackendSpreadsheetObject>& doc) = 0;

	virtual void JoinArea(
		const wxObjectDataPtr<class ibBackendSpreadsheetObject>& doc) = 0;
};

class BACKEND_API ibBackendSpreadsheetObject : public wxRefCounter {
public:

	ibBackendSpreadsheetObject() : m_docGuid(wxNewUniqueGuid), m_editable(true) {}
	ibBackendSpreadsheetObject(const ibSpreadsheetDescription& spreadsheetDesc) : m_docGuid(wxNewUniqueGuid), m_editable(true), m_spreadsheetDesc(spreadsheetDesc) {}

	ibSpreadsheetDescription& GetSpreadsheetDesc() { return m_spreadsheetDesc; }
	const ibSpreadsheetDescription& GetSpreadsheetDesc() const { return m_spreadsheetDesc; }

	bool IsEmptyDocument() const { return m_spreadsheetDesc.IsEmptySpreadsheet(); }

#pragma region __notifier_h__

	void ClearSpreadsheet(int count = 0);

	// ------ edit control functions
	//
	bool IsEditable() const { return m_editable; }
	void EnableEditing(bool edit);

	//lang 
	wxString GetLangCode() const { return m_docLangCode; }
	void SetLangCode(const wxString& strLangCode) { m_docLangCode = strLangCode; }

	//printer 
	wxString GetPrinterName() const { return m_docPrinterName; }
	void SetPrinterName(const wxString& strPrinterName) { m_docPrinterName = strPrinterName; }

	//area 
	ibSpreadsheetDescription GetArea(int rowLeft, int rowRight, int colTop = -1, int colBottom = -1);
	ibSpreadsheetDescription GetAreaByName(const wxString& strAreaLeftName, const wxString& strAreaTopName = wxT(""));

	void PutArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc);
	void JoinArea(const wxObjectDataPtr<ibBackendSpreadsheetObject>& doc);

	// ------ grid dimensions
	//

	int GetNumberRows() const { return m_spreadsheetDesc.GetNumberRows(); }
	int GetNumberCols() const { return m_spreadsheetDesc.GetNumberCols(); }

	// ------ row and col formatting
	//

	//size 
	void SetRowSize(int row, int height = 0);
	void SetColSize(int col, int width = 0);

	int GetRowSize(int row) const { return m_spreadsheetDesc.GetRowSize(row); }
	bool IsRowShown(int row) const { return m_spreadsheetDesc.IsRowShown(row); }

	int GetColSize(int col) const { return m_spreadsheetDesc.GetColSize(col); }
	bool IsColShown(int col) const { return m_spreadsheetDesc.IsColShown(col); }

	//cell
	wxColour GetCellBackgroundColour(int row, int col) const { return m_spreadsheetDesc.GetCellBackgroundColour(row, col); }
	void SetCellBackgroundColour(int row, int col, const wxColour& colour);

	wxColour GetCellTextColour(int row, int col) const { return m_spreadsheetDesc.GetCellTextColour(row, col); }
	void SetCellTextColour(int row, int col, const wxColour& colour);

	int GetCellTextOrient(int row, int col) const { return m_spreadsheetDesc.GetCellTextOrient(row, col); }
	void SetCellTextOrient(int row, int col, const int orient);

	wxFont GetCellFont(int row, int col) const { return m_spreadsheetDesc.GetCellFont(row, col); }
	void SetCellFont(int row, int col, const wxFont& font);

	void GetCellAlignment(int row, int col, int* horiz, int* vert) const { m_spreadsheetDesc.GetCellAlignment(row, col, horiz, vert); }
	void SetCellAlignment(int row, int col, const int horiz, const int vert);

	ibSpreadsheetBorderDescription GetCellBorderLeft(int row, int col) const { return m_spreadsheetDesc.GetCellBorderLeft(row, col); }
	void SetCellBorderLeft(int row, int col, const ibSpreadsheetBorderDescription& desc);

	ibSpreadsheetBorderDescription GetCellBorderRight(int row, int col) const { return m_spreadsheetDesc.GetCellBorderRight(row, col); }
	void SetCellBorderRight(int row, int col, const ibSpreadsheetBorderDescription& desc);

	ibSpreadsheetBorderDescription GetCellBorderTop(int row, int col) const { return m_spreadsheetDesc.GetCellBorderTop(row, col); }
	void SetCellBorderTop(int row, int col, const ibSpreadsheetBorderDescription& desc);

	ibSpreadsheetBorderDescription GetCellBorderBottom(int row, int col) const { return m_spreadsheetDesc.GetCellBorderBottom(row, col); }
	void SetCellBorderBottom(int row, int col, const ibSpreadsheetBorderDescription& desc);

	int GetCellSize(int row, int col, int* num_rows, int* num_cols) const { return m_spreadsheetDesc.GetCellSize(row, col, num_rows, num_cols); }
	void SetCellSize(int row, int col, int num_rows, int num_cols);

	ibSpreadsheetCellDescription::ibFitMode GetCellFitMode(int row, int col) { return m_spreadsheetDesc.GetCellFitMode(row, col); }
	void SetCellFitMode(int row, int col, ibSpreadsheetCellDescription::ibFitMode fitMode);

	bool IsCellReadOnly(int row, int col, bool isReadOnly = true) { return m_spreadsheetDesc.IsCellReadOnly(row, col); }
	void SetCellReadOnly(int row, int col, bool isReadOnly = true);

	// ------ cell brake accessors
	//
	//support printing 
	void AddRowBrake(int row);
	void AddColBrake(int col);

	void DeleteRowBrake(int row);
	void DeleteColBrake(int col);

	void SetRowBrake(int row);
	void SetColBrake(int col);

	bool IsRowBrake(int row) const { return m_spreadsheetDesc.IsRowBrake(row); }
	bool IsColBrake(int col) const { return m_spreadsheetDesc.IsColBrake(col); }

	int GetMaxRowBrake() const { return m_spreadsheetDesc.GetMaxRowBrake(); }
	int GetMaxColBrake() const { return m_spreadsheetDesc.GetMaxColBrake(); }

	// ------ freeze
	//
	void SetRowFreeze(int row);
	void SetColFreeze(int col);

	int GetRowFreeze() const { return m_spreadsheetDesc.GetRowFreeze(); }
	int GetColFreeze() const { return m_spreadsheetDesc.GetColFreeze(); }

	// ------ label and gridline formatting
	//
	int GetRowLabelSize() const { return m_spreadsheetDesc.GetRowLabelSize(); }
	int GetColLabelSize() const { return m_spreadsheetDesc.GetColLabelSize(); }

	wxFont GetLabelFont() const { return m_spreadsheetDesc.GetLabelFont(); }

	wxString GetRowLabelValue(int row) const { return m_spreadsheetDesc.GetRowLabelValue(row); }
	wxString GetColLabelValue(int col) const { return m_spreadsheetDesc.GetColLabelValue(col); }

	// ------ cell value accessors
	//

	bool IsEmptyCell(int row, int col) const { return m_spreadsheetDesc.IsEmptyCell(row, col); }

	ibSpreadsheetFillType GetCellFillType(int row, int col) const { return m_spreadsheetDesc.GetFillType(row, col); }
	void SetCellFillType(int row, int col, ibSpreadsheetFillType type);

	void GetCellValue(int row, int col, wxString& s) const { m_spreadsheetDesc.GetCellValue(row, col, s); }
	void SetCellValue(int row, int col, const wxString& s);

	//special string return 
	wxString GetCellValue(int row, int col) const { return m_spreadsheetDesc.GetCellValue(row, col); }

	// ------ cell parameter accessors
	//
	bool GetParameter(const wxString& strParameter, ibValue& valueParam) const;
	void SetParameter(const wxString& strParameter, const ibValue& valueParam = ibValue());

	wxString ComputeStringValueFromParameters(const wxString& strValue, ibSpreadsheetFillType type = ibSpreadsheetFillType::ibSpreadsheetFillType_StrParameter) const;

	//special value return 
	ibValue GetParameter(const wxString& strParameter) const { ibValue valueParam; GetParameter(strParameter, valueParam); return valueParam; }

#pragma endregion 

	void GetCellDetailsParameter(int row, int col, wxString& s) const { m_spreadsheetDesc.GetCellDetailsParameter(row, col, s); }
	void SetCellDetailsParameter(int row, int col, const wxString& s);

	bool OpenCellDetailsParameter(int row, int col) const;

#pragma region __fs_h__

	//load/save form file
	bool LoadFromFile(const wxString& strFileName);
	bool SaveToFile(const wxString& strFileName);

#pragma endregion 

	//guid 
	ibGuid GetDocGuid() const { return m_docGuid; }

#pragma region __notifier_h__

	template <typename T, typename... Args>
	wxSharedPtr<ibBackendSpreadsheetNotifier> AddNotifier(Args&&... args) {
		wxSharedPtr<ibBackendSpreadsheetNotifier> notifier(new T(std::forward<Args>(args)...));
		m_spreadsheetNotifiers.push_back(notifier);
		return notifier;
	}

	void RemoveNotifier(const wxSharedPtr<ibBackendSpreadsheetNotifier>& notify) {
		m_spreadsheetNotifiers.erase(
			std::remove(m_spreadsheetNotifiers.begin(), m_spreadsheetNotifiers.end(), notify));
	}

#pragma endregion

private:

	//localization
	wxString m_docLangCode;

	//printer
	wxString m_docPrinterName;

	// assoc with unique id
	ibGuid m_docGuid;

	// read only 
	bool m_editable;

	//cell desc
	ibSpreadsheetDescription m_spreadsheetDesc;

	//param value
	std::map<wxString, ibValue> m_paramVector;

	//grid notifier 
	wxVector<wxSharedPtr<ibBackendSpreadsheetNotifier>> m_spreadsheetNotifiers;
};

#endif 