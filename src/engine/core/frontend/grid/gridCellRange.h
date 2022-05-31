#ifndef _gridCellRange_H__
#define _gridCellRange_H__

#include <wx/grid.h>
#include <wx/headerctrl.h>
#include <wx/generic/gridctrl.h>
#include <wx/generic/grideditors.h>

class CGridCellRange
{
public:

	CGridCellRange(int nMinRow = -1, int nMinCol = -1, int nMaxRow = -1, int nMaxCol = -1)
	{
		Set(nMinRow, nMinCol, nMaxRow, nMaxCol);
	}

	void Set(int nMinRow = -1, int nMinCol = -1, int nMaxRow = -1, int nMaxCol = -1);

	bool  IsValid() const;
	bool  InRange(int row, int col) const;
	bool  InRange(const wxGridCellCoords& cellID) const;
	int  Count() { return (m_nMaxRow - m_nMinRow + 1) * (m_nMaxCol - m_nMinCol + 1); }

	wxGridCellCoords GetTopLeft() const;
	CGridCellRange Intersect(const CGridCellRange& rhs) const;

	int GetMinRow() const { return m_nMinRow; }
	void SetMinRow(int minRow) { m_nMinRow = minRow; }

	int GetMinCol() const { return m_nMinCol; }
	void SetMinCol(int minCol) { m_nMinCol = minCol; }

	int GetMaxRow() const { return m_nMaxRow; }
	void SetMaxRow(int maxRow) { m_nMaxRow = maxRow; }

	int GetMaxCol() const { return m_nMaxCol; }
	void SetMaxCol(int maxCol) { m_nMaxCol = maxCol; }

	int GetRowSpan() const { return m_nMaxRow - m_nMinRow + 1; }
	int GetColSpan() const { return m_nMaxCol - m_nMinCol + 1; }

	void operator=(const CGridCellRange& rhs);
	bool  operator==(const CGridCellRange& rhs);
	bool  operator!=(const CGridCellRange& rhs);

protected:

	int m_nMinRow;
	int m_nMinCol;
	int m_nMaxRow;
	int m_nMaxCol;
};

inline void CGridCellRange::Set(int minRow, int minCol, int maxRow, int maxCol)
{
	m_nMinRow = minRow;
	m_nMinCol = minCol;
	m_nMaxRow = maxRow;
	m_nMaxCol = maxCol;
}

inline void CGridCellRange::operator=(const CGridCellRange& rhs)
{
	Set(rhs.m_nMinRow, rhs.m_nMinCol, rhs.m_nMaxRow, rhs.m_nMaxCol);
}

inline bool CGridCellRange::operator==(const CGridCellRange& rhs)
{
	return ((m_nMinRow == rhs.m_nMinRow) && (m_nMinCol == rhs.m_nMinCol) &&
		(m_nMaxRow == rhs.m_nMaxRow) && (m_nMaxCol == rhs.m_nMaxCol));
}

inline bool CGridCellRange::operator!=(const CGridCellRange& rhs)
{
	return !operator==(rhs);
}

inline bool CGridCellRange::IsValid() const
{
	return (m_nMinRow >= 0 && m_nMinCol >= 0 && m_nMaxRow >= 0 && m_nMaxCol >= 0 &&
		m_nMinRow <= m_nMaxRow && m_nMinCol <= m_nMaxCol);
}

inline bool CGridCellRange::InRange(int row, int col) const
{
	return (row >= m_nMinRow && row <= m_nMaxRow && col >= m_nMinCol && col <= m_nMaxCol);
}

inline bool CGridCellRange::InRange(const wxGridCellCoords& cellID) const
{
	return InRange(cellID.GetRow(), cellID.GetCol());
}

inline wxGridCellCoords CGridCellRange::GetTopLeft() const
{
	return wxGridCellCoords(m_nMinRow, m_nMinCol);
}

inline CGridCellRange CGridCellRange::Intersect(const CGridCellRange& rhs) const
{
	return CGridCellRange(std::max(m_nMinRow, rhs.m_nMinRow), std::max(m_nMinCol, rhs.m_nMinCol),
		std::min(m_nMaxRow, rhs.m_nMaxRow), std::min(m_nMaxCol, rhs.m_nMaxCol));
}

#endif 