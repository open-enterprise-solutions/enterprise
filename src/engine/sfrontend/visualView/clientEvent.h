#ifndef __CLIENT_EVENT_H__
#define __CLIENT_EVENT_H__

#include "backend/fileSystem/types.h"   // s32

// WHAT HAPPENED TO A CONTROL, as a client says it — a number on the wire, a type here. The numbers are the
// protocol's: never renumbered, a new kind takes the next one.
//
// A TABLE'S CELL IS A FIELD ON A ROW: the field's events name the cell — Row (a handle) and Column (the
// column's control id) — and mean what they mean on a form's field.
enum class ibClientEvent : s32 {
	Focus   = 1,    // the control became the active one
	Command = 2,    // an entry of its command bar pressed (Id; Member — one command of a group)
	Press   = 3,    // a button pressed (Member — one command of its group)
	Input   = 4,    // typing started in a field: nothing committed, the object is modified; in a cell — its editor opened
	Change  = 5,    // a field's text committed (Text) — a cell's, and its edit is over
	Select  = 6,    // a field's Select button
	Open    = 7,    // the value opened: a field's Open button, a caption's link; a table's row (Row, no Column) — double-click / Enter
	Clear   = 8,    // a field's Clear button
	Toggle  = 9,    // a check box toggled (Checked)
	Page    = 10,   // a notebook page picked (Page: its control id)
	Move    = 11,   // dragged to another place: a notebook page's tab (Position); a table's column (Column, Holder: the group or 0, Position)
	Row     = 12,   // a table's cursor moved (Row: its handle; Column — across to that column)
	Sort    = 13,   // a table's column header clicked (Column: its control id)
	Cell    = 14,   // a spreadsheet cell clicked (Row, Col)
	Resize  = 15,   // a table column's edge dragged (Column, Width: the width it asks for)
};

#endif
