// A SHEET drawn — a spreadsheet document in the form a template is stored in (ibSpreadsheetDescriptionMemory::
// WriteNode): `cells` (`row`, `col` from 0, `value` as shown, its font, colours, alignment, borders, `rowSpan`/`colSpan`
// on the cell that spans — a cell spanned says 0 or less), `rows` and `cols` (`sizes`, `groups`, `breaks`, `areas`),
// `freeze`. A cell or a field absent is the default. Drawn as a table: the rows and columns frozen stay in view as it
// scrolls, a group of rows or of columns folds away, and a cell's text runs on over the empty cells beside it or breaks
// onto further lines, as the desktop's grid places it (placeText). Round it the labels — the rows' numbers down the
// left, the columns' along the top — a document's sheet's; a form's has nothing at its sides (`labels`) — and past what
// it holds empty cells as far as it is seen (growSheet): the thin client's sheet, on the screen; paper has neither.

import { applyFont, colour } from '../controls/control.js';

/** @typedef {{[name: string]: any}} SheetNode */

// The thin client's sheet (core/description/spreadsheetDescription.h): a column with no width of its own and a row with
// no height of its own (form.css draws it), the row labels' width.
const defaultWidth = 70;
const defaultHeight = 15;
const labelWidth = 40;

// Its outline pane (deskgui ibGrid::DrawRowOutline): a lane a level, a margin past the last; a marker a square of its row
// less an inset on each side, within its lane, no larger than its column twin and no smaller than readable.
const laneWidth = 18;
const paneMargin = 4;
const markerInset = 3;
const markerMax = 10;
const markerMin = 7;

// wx's alignment flags, as a cell keeps them.
const alignCentreHorizontal = 0x100;
const alignRight = 0x200;
const alignBottom = 0x400;
const alignCentreVertical = 0x800;

// A cell's placement (`fit`, ibSpreadsheetCellDescription::ibFitMode) and its text turned on its side (wxVERTICAL).
const fitUnset = 0;
const fitAuto = 4;
const fitWrap = 6;
const fitMask = 7;
const orientVertical = 0x0008;

// wx's pen styles, as a border keeps them: solid, dot, long dash, short dash, dot dash, transparent.
const penStyles = { 100: 'solid', 101: 'dotted', 102: 'dashed', 103: 'dashed', 104: 'dashed', 106: 'none' };

/**
 * @param {SheetNode} sheet
 * @param {{gridLines: boolean, labels: boolean, onCell: (row: number, col: number) => void}} options `labels` — round it;
 * none: laid out with no width and not shown (form.css, `bare`), so what reads them finds them still
 * @returns {HTMLTableElement}
 */
export function drawSheet(sheet, options) {
	const cells = sheet.cells ?? [];
	const rowSizes = sizesOf(sheet.rows);
	const colSizes = sizesOf(sheet.cols);
	const rowCount = Math.max(0, ...cells.map((cell) => cell.row + 1), ...[...rowSizes.keys()].map((at) => at + 1));
	const colCount = Math.max(0, ...cells.map((cell) => cell.col + 1), ...[...colSizes.keys()].map((at) => at + 1));
	const folds = foldsOf(sheet.rows?.groups ?? []).filter((fold) => fold.end < rowCount);
	const folding = folds.length > 0;
	const pane = folding ? Math.max(...folds.map((fold) => fold.level)) * laneWidth + paneMargin : 0;
	const colFolds = foldsOf(sheet.cols?.groups ?? []).filter((fold) => fold.end < colCount);
	/** @type {Map<string, SheetNode>} */
	const byPlace = new Map(cells.map((cell) => [`${cell.row}:${cell.col}`, cell]));
	const cover = coverOf(cells);

	// A frozen column stays at its place from the left edge: the outline pane, the labels and the widths before it.
	const frozenCols = sheet.freeze?.col ?? 0;
	const labelsWidth = options.labels ? labelWidth : 0;
	const lefts = [];
	let width = 0;
	const columns = document.createElement('colgroup');
	if (folding)
		columns.append(columnOf(pane, 'fold'));
	columns.append(columnOf(labelsWidth, 'label'));
	for (let col = 0; col < colCount; col++) {
		lefts.push(pane + labelsWidth + width);
		const size = colSizes.get(col) ?? defaultWidth;
		const column = columnOf(size);
		// Its place, for a fold of columns to take it away (drawColFolds) and the cells taken to pass it (selection.js).
		column.dataset.col = String(col);
		columns.append(column);
		width += size;
	}

	const table = document.createElement('table');
	table.className = 'sheet';
	table.classList.toggle('bare', !options.labels);
	// As wide as what it holds: the pane's, the labels' column and the empty ones it is filled with widen it on the
	// screen — a fixed table is the wider of its width and its columns' — and are taken off on paper (form.css). The row
	// labels stand past the pane.
	table.style.width = `${width}px`;
	table.style.setProperty('--oes-sheet-pane', `${pane}px`);
	// Where the sheet ends among the columns it is filled out with (growSheet): no text runs on past it (placeText).
	table.dataset.cols = String(colCount);
	const head = document.createElement('thead');
	const body = document.createElement('tbody');
	table.append(columns, head, body);
	// A double click on a cell is the report's — a drill-down, a document opened.
	table.addEventListener('dblclick', (event) => {
		const cell = /** @type {HTMLElement} */ (event.target).closest('td[data-row]');
		if (cell instanceof HTMLTableCellElement)
			options.onCell(Number(cell.dataset.row) + 1, Number(cell.dataset.col) + 1);
	});

	// The columns' outline over their labels, as the rows' beside theirs (ibGrid::DrawColOutline): a lane a level, its
	// corner the panes' and the labels'.
	/** @type {HTMLTableCellElement[]} */
	const colPanes = [];
	if (colFolds.length > 0) {
		const outline = document.createElement('tr');
		outline.className = 'col-folds';
		outline.style.height = `${Math.max(...colFolds.map((fold) => fold.level)) * laneWidth + paneMargin}px`;
		if (folding)
			outline.append(paneOf());
		outline.append(labelOf());
		for (let col = 0; col < colCount; col++)
			colPanes.push(colPaneOf());
		outline.append(...colPanes);
		head.append(outline);
	}

	// The column labels lead the head, the corner over the row labels; the pane runs on beside them.
	const labels = document.createElement('tr');
	labels.className = 'labels';
	if (folding)
		labels.append(paneOf());
	labels.append(labelOf());
	for (let col = 0; col < colCount; col++)
		labels.append(labelOf('col', col));
	head.append(labels);

	// A page starts at a row's print break, and is broken above it, where the desktop's grid draws its dashed line. The
	// list's largest is the sheet's end, its last row, and breaks nothing.
	const breaks = new Set(sheet.rows?.breaks ?? []);
	breaks.delete(Math.max(...breaks));
	/** @type {HTMLTableRowElement[]} */
	const rows = [];
	for (let row = 0; row < rowCount; row++) {
		const line = document.createElement('tr');
		const height = rowSizes.get(row);
		if (height !== undefined)
			line.style.height = `${height}px`;
		line.classList.toggle('page-break', breaks.has(row));
		if (folding)
			line.append(paneOf());
		line.append(labelOf('row', row));
		for (let col = 0; col < colCount; col++) {
			const cell = byPlace.get(`${row}:${col}`);
			if ((cell?.rowSpan ?? 1) <= 0 || (cell?.colSpan ?? 1) <= 0)
				continue;
			const element = cellOf(cell, row, col, height !== undefined);
			for (const [side, line] of Object.entries(linesOf(cell, row, col, cover, options.gridLines))) {
				if (line !== undefined)
					element.style[`border${side}`] = line;
			}
			// The cell after it — under a block from a row above, the block's owner
			// (ibSpreadsheetDescription::IsCellWrapped).
			const next = col + Math.max(1, cell?.colSpan ?? 1);
			let after = byPlace.get(`${row}:${next}`);
			if ((after?.rowSpan ?? 1) < 0)
				after = byPlace.get(`${row + after.rowSpan}:${next}`);
			// …and the room it has from its left to the sheet's end, should nothing after it be written (placeText).
			if (element.dataset.fit === 'auto')
				element.dataset.room = String(width - (lefts[col] - pane - labelsWidth));
			placeText(element, next < colCount && !after?.value);
			if (col < frozenCols) {
				element.classList.add('frozen');
				element.style.left = `${lefts[col]}px`;
			}
			line.append(element);
		}
		(row < (sheet.freeze?.row ?? 0) ? head : body).append(line);
		rows.push(line);
	}
	drawFolds(folds, rows, rowSizes);
	drawColFolds(colFolds, colPanes, colSizes, table, width);
	// A masked text is measured once it is laid out, and again whenever the sheet changes its size — shown from a
	// hidden tab, grown (maskText); so is Auto free to run on, against the room to the sheet's end (placeText).
	new ResizeObserver(() => {
		table.querySelectorAll('td[data-fit="mask"]').forEach(maskText);
		table.querySelectorAll('td[data-fit="auto"][data-free]').forEach((cell) => placeText(cell, true));
	}).observe(table);
	return table;
}

/**
 * THE SHEET FILLED OUT TO WHAT IS SEEN of it — empty rows and columns past what it holds, until it covers the width and
 * the height given and one of each more, as the thin client's endless sheet (ibGridEditor::FillVisibleArea): a sheet
 * looks like one wherever its frame ends, and grows on as it is scrolled. What it is filled with is the screen's: paper
 * leaves it out (form.css, `filler`). False when it covered them already.
 * @param {HTMLTableElement} table drawn by drawSheet
 * @param {number} width
 * @param {number} height
 * @param {{gridLines: boolean}} options
 */
export function growSheet(table, width, height, options) {
	const columns = /** @type {HTMLTableColElement} */ (table.querySelector('colgroup'));
	const labels = /** @type {HTMLTableRowElement} */ (table.querySelector('tr.labels'));
	const outline = table.querySelector('tr.col-folds');
	const lines = [...table.rows].filter((line) => line !== labels && line !== outline);
	const cols = Math.max(0, Math.ceil((width + defaultWidth - table.offsetWidth) / defaultWidth));
	const rows = Math.max(0, Math.ceil((height + defaultHeight - table.offsetHeight) / defaultHeight));
	if (cols === 0 && rows === 0)
		return false;
	const colCount = columns.querySelectorAll('col:not(.label):not(.fold)').length;
	const folding = columns.querySelector('col.fold') !== null;
	/**
	 * An empty cell, its lines its own: nothing beside it names one.
	 * @param {number} row @param {number} col @param {boolean} sized its row has a height of its own
	 */
	const emptyOf = (row, col, sized) => {
		const element = cellOf(undefined, row, col, sized);
		for (const [side, line] of Object.entries(linesOf(undefined, row, col, new Map(), options.gridLines))) {
			if (line !== undefined)
				element.style[`border${side}`] = line;
		}
		return element;
	};

	for (let col = colCount; col < colCount + cols; col++) {
		columns.append(columnOf(defaultWidth, 'filler'));
		outline?.append(colPaneOf());
		labels.append(labelOf('col', col));
	}
	lines.forEach((line, row) => {
		// A text at the old edge runs on over the empty cells now beside it.
		const last = line.lastElementChild;
		if (cols > 0 && last instanceof HTMLTableCellElement && last.dataset.col !== undefined
			&& Number(last.dataset.col) + last.colSpan === colCount)
			placeText(last, true);
		for (let col = colCount; col < colCount + cols; col++)
			line.append(emptyOf(row, col, line.style.height !== ''));
	});
	for (let row = lines.length; row < lines.length + rows; row++) {
		const line = document.createElement('tr');
		line.className = 'filler';
		if (folding)
			line.append(paneOf());
		line.append(labelOf('row', row));
		for (let col = 0; col < colCount + cols; col++)
			line.append(emptyOf(row, col, false));
		table.tBodies[0].append(line);
	}
	return true;
}

/**
 * A label of the strips round the sheet — a row's number down the left, a column's along the top, both from 1 as the
 * thin client's grid numbers them (ibGridEditor::GetRowLabelValue, GetColLabelValue); the corner, with neither. Its
 * place says which row or column a press on it takes (selection.js).
 * @param {'row' | 'col'} [axis]
 * @param {number} [at]
 */
function labelOf(axis, at) {
	const label = document.createElement('th');
	label.className = 'label';
	if (axis !== undefined && at !== undefined) {
		label.dataset[axis] = String(at);
		label.textContent = String(at + 1);
	}
	return label;
}

/**
 * @param {SheetNode | undefined} cell
 * @param {number} row
 * @param {number} col
 * @param {boolean} sized its row has a height of its own
 */
function cellOf(cell, row, col, sized) {
	const element = document.createElement('td');
	// Its place, for the cells taken (selection.js) and the report's double click.
	element.dataset.row = String(row);
	element.dataset.col = String(col);
	// …and how its text is placed, an empty cell's too: a text typed into it is placed as it says (setCellText).
	element.dataset.fit = fitOf(cell, sized);
	if (cell === undefined)
		return element;
	element.textContent = cell.value ?? '';
	if ((cell.rowSpan ?? 1) > 1)
		element.rowSpan = cell.rowSpan;
	if ((cell.colSpan ?? 1) > 1)
		element.colSpan = cell.colSpan;
	applyFont(element, cell.font);
	// What it does not name is the sheet's, the same in every theme: nothing to pair with the theme's.
	element.style.color = colour(cell.textColour);
	element.style.backgroundColor = colour(cell.background);
	const horizontal = cell.alignHorz ?? 0;
	element.style.textAlign = (horizontal & alignRight) !== 0 ? 'right'
		: (horizontal & alignCentreHorizontal) !== 0 ? 'center' : '';
	const vertical = cell.alignVert ?? 0;
	element.style.verticalAlign = (vertical & alignCentreVertical) !== 0 ? 'middle'
		: (vertical & alignBottom) !== 0 ? 'bottom' : '';
	// A cell a report fills with a value it can decode on a double click; one that is not to be typed into.
	element.classList.toggle('decoded', Boolean(cell.parameter));
	element.classList.toggle('read-only', cell.readOnly === true);
	return element;
}

/**
 * How a cell's text wider than the cell is placed — its `fit` (ibSpreadsheetCellDescription::ibFitMode), Auto when it
 * names none: `wrap`, onto further lines; `auto`, over the empty cell after it, else onto further lines — Auto where
 * the row's height follows its text: the row has none of its own, the cell is one row high and its text level
 * (ibSpreadsheetDescription::IsCellWrapped); `run`, Auto anywhere else — over the empty cell after it, else cut;
 * `mask`, shown as `#` where it is wider (maskText); `cut`, Clip and the ellipses.
 * @param {SheetNode | undefined} cell
 * @param {boolean} sized its row has a height of its own
 */
function fitOf(cell, sized) {
	const fit = cell?.fit ?? fitAuto;
	if (fit === fitWrap)
		return 'wrap';
	if (fit === fitMask)
		return 'mask';
	if (fit !== fitAuto && fit !== fitUnset)
		return 'cut';
	return sized || (cell?.rowSpan ?? 1) > 1 || cell?.orient === orientVertical ? 'run' : 'auto';
}

/**
 * Where a cell's text wider than the cell goes, by its fit (fitOf) and the cell after it — as the desktop's grid places
 * it: over that cell while it is empty and the text is aligned left (`spills`), onto further lines at the cell's width
 * (`wraps`), or cut at the cell's edge. Auto free to run on to the sheet's end and wider than the room there breaks
 * instead, as by a cell after it taken (ibSpreadsheetDescription::IsCellWrapped): past the last column there is no
 * paper. That is weighed once the sheet is laid out, its fonts known (drawSheet's observer); before, it runs on.
 * @param {HTMLTableCellElement} element
 * @param {boolean} free a cell after it, and empty
 */
function placeText(element, free) {
	const fit = element.dataset.fit;
	let runs = (fit === 'auto' || fit === 'run') && free && element.style.textAlign === '';
	if (runs && fit === 'auto' && element.isConnected && runsToEnd(element))
		runs = textWidth(element) <= Number(element.dataset.room);
	element.toggleAttribute('data-free', free);
	element.classList.toggle('spills', runs);
	element.classList.toggle('wraps', fit === 'wrap' || (fit === 'auto' && !runs));
}

/**
 * Whether every cell after this one in its row, to the sheet's end, is empty — a column covered from a row above
 * (rowspan) counts as written, the block's text not being looked at.
 * @param {HTMLTableCellElement} element
 */
function runsToEnd(element) {
	const end = Number(element.closest('table')?.dataset.cols ?? 0);
	let col = Number(element.dataset.col) + Math.max(1, element.colSpan);
	for (let next = element.nextElementSibling; col < end; next = next?.nextElementSibling ?? null) {
		if (!(next instanceof HTMLTableCellElement) || Number(next.dataset.col) !== col || next.textContent !== '')
			return false;
		col += Math.max(1, next.colSpan);
	}
	return true;
}

/** @type {CanvasRenderingContext2D | null} */
let measure = null;

/**
 * The width a cell's text takes drawn as it stands — its widest line and the cell's padding (ibSpreadsheetTextWidth).
 * @param {Element} element
 */
function textWidth(element) {
	const style = getComputedStyle(element);
	measure ??= document.createElement('canvas').getContext('2d');
	if (measure === null)
		return 0;
	measure.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
	const widest = Math.max(...(element.textContent ?? '').split('\n').map((line) => measure?.measureText(line).width ?? 0));
	return widest + parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
}

/**
 * A masked cell's text, where it is wider than the cell, shown as the `#` that fit in its place — one at least — as
 * the desktop's grid shows it (ibGrid::MaskTextLine). The text stays the cell's: what is copied, found, summed or
 * edited reads it (selection.js, cellEditor.js); the `#` are drawn over it (form.css, `masked`).
 * @param {Element} element
 */
function maskText(element) {
	element.classList.remove('masked');
	if (element.textContent === '' || element.scrollWidth <= element.clientWidth)
		return;
	const style = getComputedStyle(element);
	measure ??= document.createElement('canvas').getContext('2d');
	if (measure === null)
		return;
	measure.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
	const room = element.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight);
	const hash = measure.measureText('#').width || 1;
	/** @type {HTMLElement} */ (element).dataset.mask = '#'.repeat(Math.max(1, Math.floor(room / hash)));
	/** @type {HTMLElement} */ (element).style.setProperty('--oes-mask-size', style.fontSize);
	element.classList.add('masked');
}

/**
 * A cell's text changed where it stands: placed by the cell after it (placeText), and the text before it placed again
 * — it runs on over this one no longer, or again once it is empty.
 * @param {HTMLTableCellElement} cell
 * @param {string} text
 */
export function setCellText(cell, text) {
	cell.textContent = text;
	const next = cell.nextElementSibling;
	const before = cell.previousElementSibling;
	placeText(cell, next instanceof HTMLTableCellElement && next.textContent === '');
	if (cell.dataset.fit === 'mask')
		maskText(cell);
	if (before instanceof HTMLTableCellElement && before.dataset.col !== undefined)
		placeText(before, text === '');
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

/**
 * @param {number} width
 * @param {'label' | 'fold' | 'filler'} [kind] none for a column of the sheet's own
 */
function columnOf(width, kind) {
	const column = document.createElement('col');
	column.style.width = `${width}px`;
	if (kind !== undefined)
		column.className = kind;
	return column;
}

/** A row's or a column's sizes by its place. @param {SheetNode | undefined} axis */
function sizesOf(axis) {
	return new Map((axis?.sizes ?? []).map((one) => [one.at, one.size]));
}

/** @typedef {{head: number, start: number, end: number, level: number, collapsed: boolean}} Fold */

/**
 * THE GROUPS AS THE THIN CLIENT'S GRID SHAPES THEM (ibGrid::NormalizeRowGroups). A sheet's groups come as headings with a
 * depth each: a heading folds the run of deeper ones after it and stays in sight itself, its marker on its first row; a
 * heading nothing deeper follows has nothing to fold and no marker. One that came collapsed is folded.
 * @param {{start: number, end: number, level: number, collapsed?: boolean}[]} groups
 * @returns {Fold[]}
 */
function foldsOf(groups) {
	/** @type {Fold[]} */
	const folds = [];
	groups.forEach((heading, at) => {
		let end = heading.end;
		for (let next = at + 1; next < groups.length && groups[next].level > heading.level; next++)
			end = groups[next].end;
		if (end > heading.end)
			folds.push({ head: heading.start, start: heading.end + 1, end, level: heading.level, collapsed: heading.collapsed === true });
	});
	return folds;
}

/** A row's cell of the outline pane — the folds draw into it (drawFolds). */
function paneOf() {
	const element = document.createElement('td');
	element.className = 'fold';
	return element;
}

/** A column's cell of the outline over the labels — the folds of columns draw into it (drawColFolds). */
function colPaneOf() {
	const element = document.createElement('td');
	element.className = 'col-fold';
	return element;
}

/**
 * A fold's marker — a chevron, pointing down while open, on along the fold while closed, its arms a third of its side
 * and more, its depth half of that (ibThemeRenderer::DrawTreeItemButton): the rows' and the columns' alike, as the thin
 * client's grid draws both with the one button. Pressed, it turns the fold and says so.
 * @param {Fold} fold
 * @param {number} size
 * @param {() => void} turned
 */
function markerOf(fold, size, turned) {
	const button = document.createElement('button');
	button.type = 'button';
	button.style.width = `${size}px`;
	button.style.height = `${size}px`;
	const chevron = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
	const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
	svg.setAttribute('viewBox', '0 0 1 1');
	svg.append(chevron);
	button.append(svg);
	const shape = () => {
		button.setAttribute('aria-expanded', String(!fold.collapsed));
		chevron.setAttribute('points', fold.collapsed ? '0.32,0.14 0.68,0.5 0.32,0.86' : '0.14,0.32 0.5,0.68 0.86,0.32');
	};
	shape();
	button.addEventListener('click', () => {
		fold.collapsed = !fold.collapsed;
		shape();
		turned();
	});
	return button;
}

/**
 * THE OUTLINE, as the thin client's pane draws it (ibGrid::DrawRowOutline): in its level's lane, a chevron on the row
 * that opens a fold — open, it points down — and, while open, its rail from under the chevron down to the fold's last
 * row and a tick from there to the pane's edge. A chevron pressed folds its rows away or brings them back, but for those
 * a fold of their own keeps closed; a marker on a row folded away goes with it.
 * @param {Fold[]} folds
 * @param {HTMLTableRowElement[]} rows the sheet's, each led by its pane cell
 * @param {Map<number, number>} rowSizes
 */
function drawFolds(folds, rows, rowSizes) {
	const cellAt = (/** @type {number} */ row) => /** @type {HTMLTableCellElement} */ (rows[row].cells[0]);
	/** The rows the folds closed now keep away, shown or hidden from one row to another. @param {number} from @param {number} to */
	const hide = (from, to) => {
		const hidden = new Array(rows.length).fill(false);
		for (const fold of folds) {
			if (fold.collapsed)
				hidden.fill(true, fold.start, fold.end + 1);
		}
		for (let row = from; row <= to; row++)
			rows[row].hidden = hidden[row];
	};
	for (const fold of folds) {
		const lane = (fold.level - 1) * laneWidth;
		const size = Math.max(markerMin, Math.min(laneWidth - markerInset * 2,
			(rowSizes.get(fold.head) ?? defaultHeight) - markerInset * 2, markerMax));
		const x = lane + Math.floor((laneWidth - size) / 2);
		const railX = x + Math.floor(size / 2);

		const button = markerOf(fold, size, () => hide(fold.start, fold.end));
		button.style.left = `${x}px`;
		button.style.top = `calc(50% - ${size / 2}px)`;
		const stem = railOf(railX);
		stem.style.top = `calc(50% + ${Math.ceil(size / 2)}px)`;
		cellAt(fold.head).append(button, stem);

		for (let row = fold.start; row < fold.end; row++)
			cellAt(row).append(railOf(railX));
		const tick = document.createElement('span');
		tick.className = 'tick';
		tick.style.left = `${railX}px`;
		cellAt(fold.end).append(railOf(railX), tick);
	}
	if (folds.some((fold) => fold.collapsed))
		hide(0, rows.length - 1);
}

/**
 * THE COLUMNS' OUTLINE, as the thin client's (ibGrid::DrawColOutline) — the rows' turned on its side: in its level's
 * lane over the labels, a chevron on the column that opens a fold and, while open, its rail across to the fold's last
 * column and a tick from there down to the labels. Pressed, the fold's columns go away or come back, but for those a fold
 * of their own keeps closed — a marker on a column gone goes with it; the table narrows by them, or a fixed table would
 * spread what is left over its width.
 * @param {Fold[]} folds
 * @param {HTMLTableCellElement[]} cells the outline's, a column each
 * @param {Map<number, number>} colSizes
 * @param {HTMLTableElement} table
 * @param {number} width the sheet's columns', all shown
 */
function drawColFolds(folds, cells, colSizes, table, width) {
	const columns = /** @type {HTMLTableColElement[]} */ ([...table.querySelectorAll('col[data-col]')]);
	const hide = () => {
		const away = new Array(columns.length).fill(false);
		for (const fold of folds) {
			if (fold.collapsed)
				away.fill(true, fold.start, fold.end + 1);
		}
		let shown = width;
		columns.forEach((column, col) => {
			column.classList.toggle('folded', away[col]);
			if (away[col])
				shown -= colSizes.get(col) ?? defaultWidth;
		});
		table.style.width = `${shown}px`;
	};
	for (const fold of folds) {
		const lane = (fold.level - 1) * laneWidth;
		const headWidth = colSizes.get(fold.head) ?? defaultWidth;
		const size = Math.max(markerMin, Math.min(laneWidth - markerInset * 2, headWidth - markerInset * 2, markerMax));
		const x = Math.max(0, Math.floor((headWidth - size) / 2));
		const y = lane + Math.floor((laneWidth - size) / 2);
		const railY = y + Math.floor(size / 2);

		/** @type {HTMLElement[]} the rail and its tick — a fold closed has neither */
		const rails = [];
		const rail = (/** @type {number} */ col, /** @type {number} */ from) => {
			const element = document.createElement('span');
			element.className = 'rail-across';
			element.style.left = `${from}px`;
			element.style.top = `${railY}px`;
			cells[col].append(element);
			rails.push(element);
		};
		const button = markerOf(fold, size, () => {
			rails.forEach((element) => { element.hidden = fold.collapsed; });
			hide();
		});
		button.style.left = `${x}px`;
		button.style.top = `${y}px`;
		cells[fold.head].append(button);
		rail(fold.head, x + size);
		for (let col = fold.head + 1; col <= fold.end; col++)
			rail(col, 0);
		const tick = document.createElement('span');
		tick.className = 'tick-down';
		tick.style.top = `${railY}px`;
		cells[fold.end].append(tick);
		rails.push(tick);
		rails.forEach((element) => { element.hidden = fold.collapsed; });
	}
	if (folds.some((fold) => fold.collapsed))
		hide();
}

/** A rail down a pane cell, at its lane's middle. @param {number} x */
function railOf(x) {
	const rail = document.createElement('span');
	rail.className = 'rail';
	rail.style.left = `${x}px`;
	return rail;
}
