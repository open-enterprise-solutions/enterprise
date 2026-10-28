// A TABLE'S ROWS as the protocol gives them (docs/public/client-protocol.md, The frame): a page — `End`, `Before`,
// `CurrentRow`, the rows as its children — each row by its handle (NodeId) with `Container`, `Group`, `ReadOnly`,
// `Picture`, its cells as children by the column's id: `Text`, `Checked` for a flag, `Number`, `Value` when the cell
// takes an edit, its look. Turned here into the grid's records, one field per column and what a cell says of itself.

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

/**
 * A record of the grid: the row's handle and its own fields, and per column `c<id>` its text, with `n<id>` a number,
 * `k<id>` a flag's mark, `e<id>` editable, `s<id>` its look.
 * @typedef {{[field: string]: any, handle: number}} Record
 */

/**
 * @param {ProtocolNode} row
 * @returns {Record}
 */
export function recordOf(row) {
	/** @type {Record} */
	const record = {
		handle: row.NodeId ?? 0,
		container: row.Container === true,
		group: row.Group ?? '',
		readOnly: row.ReadOnly === true,
		picture: row.Picture ?? '',
	};
	for (const cell of row.NodeChildren ?? []) {
		const id = cell.NodeId;
		record[`c${id}`] = cell.Text ?? '';
		if (cell.Number === true)
			record[`n${id}`] = true;
		if (cell.Checked !== undefined)
			record[`k${id}`] = cell.Checked === true;
		if (cell.Value !== undefined)
			record[`e${id}`] = true;
		const look = lookOf(cell);
		if (look !== null)
			record[`s${id}`] = look;
	}
	return record;
}

/**
 * A cell's look, laid over the table's — the colours, the font, where its text stands; null when it has none.
 * @param {ProtocolNode} cell
 */
function lookOf(cell) {
	const look = {};
	if (cell.TextColour)
		look.color = `rgb(${cell.TextColour})`;
	if (cell.BackgroundColour)
		look.backgroundColor = `rgb(${cell.BackgroundColour})`;
	if (cell.Bold)
		look.fontWeight = 'bold';
	if (cell.Italic)
		look.fontStyle = 'italic';
	if (cell.Strikethrough || cell.Underlined)
		look.textDecoration = [cell.Strikethrough ? 'line-through' : '', cell.Underlined ? 'underline' : ''].join(' ').trim();
	if (cell.Face)
		look.fontFamily = `"${String(cell.Face).replace(/"/g, '')}", var(--oes-font-ui)`;
	if (cell.Size)
		look.fontSize = `${cell.Size}pt`;
	if (cell.Align)
		look.textAlign = String(cell.Align).toLowerCase();
	return Object.keys(look).length > 0 ? look : null;
}

/**
 * The page's records and what it says of itself — a State's page also the row it was read around (Anchor) and how
 * many rows it asked from it, as many ahead (Count).
 * @param {ProtocolNode} page
 */
export function readPage(page) {
	return {
		records: (page.NodeChildren ?? []).map(recordOf),
		end: page.End === true,
		before: page.Before ?? 0,
		current: page.CurrentRow ?? 0,
		anchor: page.Anchor ?? 0,
		count: page.Count ?? 0,
	};
}
