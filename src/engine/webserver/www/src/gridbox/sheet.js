// A SHEET drawn — a spreadsheet document in the form a template is stored in (ibSpreadsheetDescriptionMemory::
// WriteNode): `cells` (`row`, `col` from 0, `value` as shown, its font, colours, alignment, borders, `rowSpan`/`colSpan`
// on the cell that spans — a cell spanned says 0 or less), `rows` and `cols` (`sizes`, `groups`, `breaks`, `areas`),
// `freeze`. A cell or a field absent is the default. Drawn as a table: the rows and columns frozen stay in view as it
// scrolls, a row group folds away, and a cell's text runs on over the empty cells beside it, as the desktop's grid
// draws it.

import { applyFont, colour } from '../controls/control.js';

/** @typedef {{[name: string]: any}} SheetNode */

// The desktop grid's width of a column that has none of its own (WXGRID_DEFAULT_COL_WIDTH); the fold buttons' column.
const defaultWidth = 80;
const foldWidth = 20;

// wx's alignment flags, as a cell keeps them.
const alignCentreHorizontal = 0x100;
const alignRight = 0x200;
const alignBottom = 0x400;
const alignCentreVertical = 0x800;

// wx's pen styles, as a border keeps them: solid, dot, long dash, short dash, dot dash, transparent.
const penStyles = { 100: 'solid', 101: 'dotted', 102: 'dashed', 103: 'dashed', 104: 'dashed', 106: 'none' };

/**
 * @param {SheetNode} sheet
 * @param {{gridLines: boolean, onCell: (row: number, col: number) => void}} options
 * @returns {HTMLTableElement}
 */
export function drawSheet(sheet, options) {
	const cells = sheet.cells ?? [];
	const rowSizes = sizesOf(sheet.rows);
	const colSizes = sizesOf(sheet.cols);
	const rowCount = Math.max(0, ...cells.map((cell) => cell.row + 1), ...[...rowSizes.keys()].map((at) => at + 1));
	const colCount = Math.max(0, ...cells.map((cell) => cell.col + 1), ...[...colSizes.keys()].map((at) => at + 1));
	const levels = levelsOf(sheet.rows?.groups ?? [], rowCount);
	const folding = levels.some((level) => level > 0);
	/** @type {Map<string, SheetNode>} */
	const byPlace = new Map(cells.map((cell) => [`${cell.row}:${cell.col}`, cell]));
	const cover = coverOf(cells);

	// A frozen column stays at its place from the left edge: the widths before it, the folds' column first.
	const frozenCols = sheet.freeze?.col ?? 0;
	const lefts = [];
	let width = folding ? foldWidth : 0;
	const columns = document.createElement('colgroup');
	if (folding)
		columns.append(columnOf(foldWidth));
	for (let col = 0; col < colCount; col++) {
		lefts.push(width);
		const size = colSizes.get(col) ?? defaultWidth;
		columns.append(columnOf(size));
		width += size;
	}

	const table = document.createElement('table');
	table.className = 'sheet';
	table.style.width = `${width}px`;
	const head = document.createElement('thead');
	const body = document.createElement('tbody');
	table.append(columns, head, body);

	// A row's print break ends a page under it, where the desktop's grid draws its dashed line.
	const breaks = new Set(sheet.rows?.breaks ?? []);
	/** @type {HTMLTableRowElement[]} */
	const rows = [];
	for (let row = 0; row < rowCount; row++) {
		const line = document.createElement('tr');
		const height = rowSizes.get(row);
		if (height !== undefined)
			line.style.height = `${height}px`;
		line.classList.toggle('page-break', breaks.has(row));
		if (folding)
			line.append(foldOf(row, rows, levels));
		for (let col = 0; col < colCount; col++) {
			const cell = byPlace.get(`${row}:${col}`);
			if ((cell?.rowSpan ?? 1) <= 0 || (cell?.colSpan ?? 1) <= 0)
				continue;
			const element = cellOf(cell, row, col, options);
			for (const [side, line] of Object.entries(linesOf(cell, row, col, cover, options.gridLines))) {
				if (line !== undefined)
					element.style[`border${side}`] = line;
			}
			const next = col + Math.max(1, cell?.colSpan ?? 1);
			element.classList.toggle('spills', next < colCount && !byPlace.get(`${row}:${next}`)?.value);
			if (col < frozenCols) {
				element.classList.add('frozen');
				element.style.left = `${lefts[col]}px`;
			}
			line.append(element);
		}
		(row < (sheet.freeze?.row ?? 0) ? head : body).append(line);
		rows.push(line);
	}
	return table;
}

/**
 * @param {SheetNode | undefined} cell
 * @param {number} row
 * @param {number} col
 * @param {{onCell: (row: number, col: number) => void}} options
 */
function cellOf(cell, row, col, options) {
	const element = document.createElement('td');
	// Its place, for the cells taken (selection.js); a double click is the report's.
	element.dataset.row = String(row);
	element.dataset.col = String(col);
	element.addEventListener('dblclick', () => options.onCell(row + 1, col + 1));
	if (cell === undefined)
		return element;
	element.textContent = cell.value ?? '';
	if ((cell.rowSpan ?? 1) > 1)
		element.rowSpan = cell.rowSpan;
	if ((cell.colSpan ?? 1) > 1)
		element.colSpan = cell.colSpan;
	applyFont(element, cell.font);
	element.style.color = colour(cell.textColour);
	element.style.backgroundColor = colour(cell.background);
	const horizontal = cell.alignHorz ?? 0;
	element.style.textAlign = (horizontal & alignRight) !== 0 ? 'right'
		: (horizontal & alignCentreHorizontal) !== 0 ? 'center' : '';
	const vertical = cell.alignVert ?? 0;
	element.style.verticalAlign = (vertical & alignCentreVertical) !== 0 ? 'middle'
		: (vertical & alignBottom) !== 0 ? 'bottom' : '';
	// A cell a report fills with a value it can decode on a double click.
	element.classList.toggle('decoded', Boolean(cell.parameter));
	return element;
}

/**
 * The lines a cell draws. ONE LINE BETWEEN TWO CELLS, drawn once: the desktop's grid draws on the line two cells share
 * the border either of them names, and the table here keeps its cells' borders apart (collapsed, Chromium paints the
 * rows scrolled under a frozen head over it). So a cell draws its right and its bottom — its own border there, else the
 * one the cell beyond names, else the grid's — and its top and its left only at the sheet's edge. The folds' column is
 * a margin, as the desktop's: it draws none.
 * @param {SheetNode | undefined} cell
 * @param {number} row
 * @param {number} col
 * @param {Map<string, SheetNode>} cover
 * @param {boolean} gridLines
 */
function linesOf(cell, row, col, cover, gridLines) {
	const grid = gridLines ? '1px solid var(--oes-table-border)' : undefined;
	const below = row + Math.max(1, cell?.rowSpan ?? 1);
	const beyond = col + Math.max(1, cell?.colSpan ?? 1);
	return {
		Top: row === 0 ? lineOf(cell?.top) ?? grid : undefined,
		Left: col === 0 ? lineOf(cell?.left) ?? grid : undefined,
		Right: lineOf(cell?.right) ?? lineOf(cover.get(`${row}:${beyond}`)?.left) ?? grid,
		Bottom: lineOf(cell?.bottom) ?? lineOf(cover.get(`${below}:${col}`)?.top) ?? grid,
	};
}

/** A border a cell keeps, as CSS; none for one that draws nothing. @param {SheetNode | undefined} border */
function lineOf(border) {
	const style = penStyles[border?.style] ?? 'solid';
	if (border === undefined || style === 'none')
		return undefined;
	return `${border.width ?? 1}px ${style} ${colour(border.colour) || 'currentColor'}`;
}

/** The cell over each place, by `row:col` — a spanning cell over every place it spans. @param {SheetNode[]} cells */
function coverOf(cells) {
	/** @type {Map<string, SheetNode>} */
	const cover = new Map();
	for (const cell of cells) {
		if ((cell.rowSpan ?? 1) <= 0 || (cell.colSpan ?? 1) <= 0)
			continue;
		for (let row = cell.row; row < cell.row + (cell.rowSpan ?? 1); row++) {
			for (let col = cell.col; col < cell.col + (cell.colSpan ?? 1); col++)
				cover.set(`${row}:${col}`, cell);
		}
	}
	return cover;
}

/** @param {number} width */
function columnOf(width) {
	const column = document.createElement('col');
	column.style.width = `${width}px`;
	return column;
}

/** A row's or a column's sizes by its place. @param {SheetNode | undefined} axis */
function sizesOf(axis) {
	return new Map((axis?.sizes ?? []).map((one) => [one.at, one.size]));
}

/**
 * Each row's outline level: the deepest group it stands in, 0 for none.
 * @param {{start: number, end: number, level: number}[]} groups
 * @param {number} count
 */
function levelsOf(groups, count) {
	const levels = new Array(count).fill(0);
	for (const group of groups) {
		for (let row = group.start; row <= group.end && row < count; row++)
			levels[row] = Math.max(levels[row], group.level);
	}
	return levels;
}

/**
 * The fold's cell of a row: a button when the rows after it go deeper. Pressed, it hides them down to the next row
 * as shallow as it, and shows them again — but for those a fold of their own still keeps closed.
 * @param {number} at
 * @param {HTMLTableRowElement[]} rows all the sheet's rows, by the time the button is pressed
 * @param {number[]} levels
 */
function foldOf(at, rows, levels) {
	const element = document.createElement('td');
	element.className = 'fold';
	if (at + 1 >= levels.length || levels[at + 1] <= levels[at])
		return element;
	const button = document.createElement('button');
	button.type = 'button';
	button.textContent = '−';
	button.setAttribute('aria-expanded', 'true');
	button.addEventListener('click', () => {
		const open = button.getAttribute('aria-expanded') !== 'true';
		button.setAttribute('aria-expanded', String(open));
		button.textContent = open ? '−' : '+';
		// Rows deeper than a closed fold above them are hidden; the next row as shallow as that fold ends its hold.
		let under = Infinity;
		rows.forEach((row, place) => {
			row.hidden = levels[place] > under;
			if (!row.hidden)
				under = row.querySelector('.fold button')?.getAttribute('aria-expanded') === 'false' ? levels[place] : Infinity;
		});
	});
	element.append(button);
	return element;
}
