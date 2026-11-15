// CELLS TAKEN, as in a spreadsheet program. The first cell is the current one until another is, as the thin client's
// cursor. A click takes a cell, a drag a block from it, Shift a block from the cell taken first, Ctrl one block more; a
// row's label takes the row, a column's the column, the corner the sheet; the arrows move the cell taken, Shift with
// them stretches the block; Ctrl+A takes the sheet, Ctrl+C copies what is taken — as text by tabs and lines and as a
// table, so a spreadsheet program pastes it into cells, a figure as a number whatever its decimal sign. A cell spanning
// several places takes its whole span. What is taken is what is seen: a row or a column folded away is neither summed nor
// copied, nor stopped on. The figures taken are summed in a line over the sheet's corner — average, count, sum.

import { _, format } from '../localization.js';

/** @typedef {{row: number, col: number}} Place */
/** @typedef {{top: number, left: number, bottom: number, right: number}} Block */

export class CellSelection {
	/**
	 * @param {HTMLElement} host the sheet's scrolling frame — it takes the keys
	 * @param {HTMLElement} summary the line the figures taken are summed in
	 */
	constructor(host, summary) {
		this.host = host;
		this.summary = summary;
		host.tabIndex = 0;
		/** @type {HTMLTableElement} the sheet's, from the first attach */
		this.table = /** @type {any} */ (null);
		/** @type {Map<string, HTMLTableCellElement>} the cell over each place, by `row:col` */
		this.cells = new Map();
		/** @type {HTMLTableRowElement[]} */
		this.rows = [];
		/** @type {Block[]} the blocks taken before the last one, with Ctrl */
		this.kept = [];
		/** @type {Place | null} the cell taken first — the current one */
		this.anchor = null;
		/** @type {Place} …and the corner the last block is stretched to */
		this.focus = { row: 0, col: 0 };
		/** @type {Set<HTMLTableCellElement>} */
		this.marked = new Set();
		/** @type {HTMLTableCellElement | undefined} */
		this.current = undefined;
		host.addEventListener('mousedown', (event) => this.pressed(event));
		host.addEventListener('mouseover', (event) => this.dragged(event));
		host.addEventListener('keydown', (event) => this.key(event));
	}

	/** A sheet drawn anew: nothing taken on it, its first cell the current one. @param {HTMLTableElement} table */
	attach(table) {
		this.table = table;
		this.grown();
		this.kept = [];
		this.anchor = this.cells.has('0:0') ? { row: 0, col: 0 } : null;
		this.focus = { row: 0, col: 0 };
		this.marked.clear();
		this.current = undefined;
		this.paint();
	}

	/** The sheet filled out further (growSheet): its new cells known, what is taken kept. */
	grown() {
		this.cells.clear();
		this.rows = [...this.table.rows].filter((line) => !line.matches('.labels, .col-folds'));
		for (const cell of /** @type {NodeListOf<HTMLTableCellElement>} */ (this.table.querySelectorAll('td[data-row]'))) {
			const { row, col } = placeOf(cell);
			for (let r = row; r < row + cell.rowSpan; r++) {
				for (let c = col; c < col + cell.colSpan; c++)
					this.cells.set(`${r}:${c}`, cell);
			}
		}
	}

	/**
	 * What the cells taken stand at — after the sheet is drawn again, each where it was.
	 * @param {Place | null} anchor the current cell
	 * @param {Place} focus the corner the last block is stretched to
	 * @param {Block[]} [kept] the blocks taken before it
	 */
	take(anchor, focus, kept = []) {
		this.kept = kept;
		this.anchor = anchor;
		this.focus = focus;
		this.paint();
	}

	/** @param {MouseEvent} event */
	pressed(event) {
		// A press in a cell's text being typed is the text's.
		if (event.button !== 0 || /** @type {HTMLElement} */ (event.target).closest('.cell-editor') !== null)
			return;
		const label = /** @type {HTMLElement} */ (event.target).closest('th.label');
		if (label instanceof HTMLTableCellElement) {
			this.pressedLabel(label, event);
			return;
		}
		const cell = /** @type {HTMLElement} */ (event.target).closest('td[data-row]');
		if (cell === null)
			return;
		const place = placeOf(/** @type {HTMLTableCellElement} */ (cell));
		if (event.shiftKey && this.anchor !== null) {
			this.focus = place;
		}
		else {
			if ((event.ctrlKey || event.metaKey) && this.anchor !== null)
				this.kept.push(this.block(this.anchor, this.focus));
			else
				this.kept = [];
			this.anchor = place;
			this.focus = place;
		}
		this.paint();
	}

	/**
	 * A label pressed: its row, its column — the corner, the sheet — taken whole from its first cell, which is the
	 * current one; with Ctrl, one block more.
	 * @param {HTMLTableCellElement} label
	 * @param {MouseEvent} event
	 */
	pressedLabel(label, event) {
		const row = label.dataset.row === undefined ? undefined : Number(label.dataset.row);
		const col = label.dataset.col === undefined ? undefined : Number(label.dataset.col);
		if ((event.ctrlKey || event.metaKey) && this.anchor !== null)
			this.kept.push(this.block(this.anchor, this.focus));
		else
			this.kept = [];
		this.anchor = { row: row ?? 0, col: col ?? 0 };
		this.focus = { row: row ?? this.rows.length - 1, col: col ?? this.lastCol() };
		this.paint();
	}

	/** A drag, the button held: the last block stretched to the cell under it. @param {MouseEvent} event */
	dragged(event) {
		const cell = /** @type {HTMLElement} */ (event.target).closest('td[data-row]');
		if ((event.buttons & 1) === 0 || cell === null || this.anchor === null
			|| /** @type {HTMLElement} */ (event.target).closest('.cell-editor') !== null)
			return;
		const place = placeOf(/** @type {HTMLTableCellElement} */ (cell));
		if (place.row === this.focus.row && place.col === this.focus.col)
			return;
		this.focus = place;
		this.paint();
	}

	/** The sheet's keys — not those of a cell's text being typed. @param {KeyboardEvent} event */
	key(event) {
		if (event.target !== this.host)
			return;
		const ctrl = event.ctrlKey || event.metaKey;
		if (ctrl && event.code === 'KeyA') {
			event.preventDefault();
			this.selectAll();
			return;
		}
		if (ctrl && event.code === 'KeyC') {
			event.preventDefault();
			this.copy();
			return;
		}
		const step = { ArrowUp: [-1, 0], ArrowDown: [1, 0], ArrowLeft: [0, -1], ArrowRight: [0, 1] }[event.key];
		if (step === undefined || ctrl)
			return;
		event.preventDefault();
		this.moveBy(step, event.shiftKey);
	}

	/** Ctrl+A, Edit → Select All: one block over every cell, from the first. */
	selectAll() {
		this.kept = [];
		this.anchor = { row: 0, col: 0 };
		this.focus = { row: this.rows.length - 1, col: this.lastCol() };
		this.paint();
	}

	/**
	 * The current cell a step away — or, stretched, the block's far corner — and brought into view.
	 * @param {number[]} step rows down, columns across
	 * @param {boolean} stretch
	 */
	moveBy(step, stretch) {
		if (this.anchor === null) {
			this.anchor = { row: 0, col: 0 };
			this.focus = this.anchor;
		}
		else if (stretch) {
			this.focus = this.next(this.focus, step);
		}
		else {
			this.kept = [];
			this.anchor = this.next(this.anchor, step);
			this.focus = this.anchor;
		}
		this.paint();
		// Brought into view clear of the labels and the frozen rows, which stand over the top of the frame, and of the
		// outline and the row labels at its left, as far as the corner's right edge.
		const head = this.table?.tHead;
		const corner = /** @type {HTMLElement | null | undefined} */ (head?.rows[0]?.querySelector('th.label:not([data-col])'));
		this.host.style.scrollPaddingTop = `${head?.offsetHeight ?? 0}px`;
		this.host.style.scrollPaddingLeft = `${corner ? corner.offsetLeft + corner.offsetWidth : 0}px`;
		this.cells.get(key(stretch ? this.focus : this.anchor))?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
	}

	/**
	 * The cell a step away: past the cell it starts on when that spans, past a row folded away, and where it stands
	 * when there is nothing further.
	 * @param {Place} from
	 * @param {number[]} step
	 * @returns {Place}
	 */
	next(from, [down, across]) {
		const start = this.cells.get(key(from));
		const folded = this.foldedCols();
		for (let row = from.row + down, col = from.col + across; ; row += down, col += across) {
			const cell = this.cells.get(`${row}:${col}`);
			if (cell === undefined)
				return from;
			if (cell !== start && !this.rows[row].hidden && !folded.has(col))
				return placeOf(cell);
		}
	}

	/** The columns a fold keeps away now (sheet.js, drawColFolds). @returns {Set<number>} */
	foldedCols() {
		return new Set([...(this.table?.querySelectorAll('col.folded') ?? [])]
			.map((column) => Number(/** @type {HTMLElement} */ (column).dataset.col)));
	}

	/** The last column any row reaches. */
	lastCol() {
		let last = 0;
		for (const place of this.cells.keys())
			last = Math.max(last, Number(place.split(':')[1]));
		return last;
	}

	/**
	 * The block between two corners, grown over every cell that spans out of it.
	 * @param {Place} one
	 * @param {Place} other
	 * @returns {Block}
	 */
	block(one, other) {
		const block = {
			top: Math.min(one.row, other.row), left: Math.min(one.col, other.col),
			bottom: Math.max(one.row, other.row), right: Math.max(one.col, other.col),
		};
		for (let grown = true; grown;) {
			grown = false;
			for (const cell of this.cellsIn(block)) {
				const { row, col } = placeOf(cell);
				const bottom = row + cell.rowSpan - 1;
				const right = col + cell.colSpan - 1;
				if (row < block.top || col < block.left || bottom > block.bottom || right > block.right) {
					block.top = Math.min(block.top, row);
					block.left = Math.min(block.left, col);
					block.bottom = Math.max(block.bottom, bottom);
					block.right = Math.max(block.right, right);
					grown = true;
				}
			}
		}
		return block;
	}

	/** Each cell over a block's places, once. @param {Block} block */
	cellsIn(block) {
		/** @type {Set<HTMLTableCellElement>} */
		const cells = new Set();
		for (let row = block.top; row <= block.bottom; row++) {
			for (let col = block.left; col <= block.right; col++) {
				const cell = this.cells.get(`${row}:${col}`);
				if (cell !== undefined)
					cells.add(cell);
			}
		}
		return cells;
	}

	/** @returns {Block[]} */
	blocks() {
		return this.anchor === null ? [] : [...this.kept, this.block(this.anchor, this.focus)];
	}

	/** The cells taken marked — tinted when more than one is, the current one framed — and the figures summed. */
	paint() {
		/** @type {Set<HTMLTableCellElement>} */
		const taken = new Set();
		for (const block of this.blocks()) {
			for (const cell of this.cellsIn(block))
				taken.add(cell);
		}
		const tinted = taken.size > 1 ? taken : new Set();
		for (const cell of this.marked) {
			if (!tinted.has(cell))
				cell.classList.remove('selected');
		}
		for (const cell of tinted)
			cell.classList.add('selected');
		this.marked = tinted;
		this.current?.classList.remove('current');
		this.current = this.anchor === null ? undefined : this.cells.get(key(this.anchor));
		this.current?.classList.add('current');
		const folded = this.foldedCols();
		this.sum([...taken].filter((cell) => !/** @type {HTMLElement} */ (cell.parentElement).hidden
			&& !folded.has(placeOf(cell).col)));
	}

	/** @param {HTMLTableCellElement[]} cells the cells taken and seen */
	sum(cells) {
		let count = 0;
		let figures = 0;
		let total = 0;
		let digits = 0;
		for (const cell of cells) {
			const text = cell.textContent?.trim() ?? '';
			if (text === '')
				continue;
			count++;
			const figure = figureOf(text);
			if (figure === undefined)
				continue;
			figures++;
			total += figure.value;
			digits = Math.max(digits, figure.digits);
		}
		this.summary.hidden = count < 2;
		const counted = format(_('Count: %d'), count);
		this.summary.textContent = figures === 0 ? counted : [format(_('Average: %s'), (total / figures).toFixed(digits)),
			counted, format(_('Sum: %s'), total.toFixed(digits))].join('    ');
	}

	/** What is taken onto the clipboard: the rows and columns the blocks cover, a place not taken left empty. */
	copy() {
		const blocks = this.blocks();
		if (blocks.length === 0)
			return;
		const top = Math.min(...blocks.map((block) => block.top));
		const left = Math.min(...blocks.map((block) => block.left));
		const bottom = Math.max(...blocks.map((block) => block.bottom));
		const right = Math.max(...blocks.map((block) => block.right));
		const taken = (/** @type {number} */ row, /** @type {number} */ col) => blocks.some((block) =>
			row >= block.top && row <= block.bottom && col >= block.left && col <= block.right);
		const folded = this.foldedCols();
		/** @type {string[][]} */
		const lines = [];
		for (let row = top; row <= bottom; row++) {
			if (this.rows[row].hidden)
				continue;
			const line = [];
			for (let col = left; col <= right; col++) {
				if (folded.has(col))
					continue;
				const cell = this.cells.get(`${row}:${col}`);
				// A spanning cell says its text once, at its first place.
				const first = cell !== undefined && placeOf(cell).row === row && placeOf(cell).col === col;
				line.push(first && taken(row, col) ? (cell.textContent ?? '') : '');
			}
			lines.push(line);
		}
		const text = lines.map((line) => line.join('\t')).join('\r\n');
		const html = `<table>${lines.map((line) => `<tr>${line.map(htmlCell).join('')}</tr>`).join('')}</table>`;
		const put = (/** @type {ClipboardEvent} */ event) => {
			event.clipboardData?.setData('text/plain', text);
			event.clipboardData?.setData('text/html', html);
			event.preventDefault();
		};
		// The copy event of the page, not the clipboard's promise: it asks for no secure origin.
		document.addEventListener('copy', put, { once: true });
		document.execCommand('copy');
		document.removeEventListener('copy', put);
	}
}

/** @param {HTMLTableCellElement} cell @returns {Place} */
function placeOf(cell) {
	return { row: Number(cell.dataset.row), col: Number(cell.dataset.col) };
}

/** @param {Place} place */
function key(place) {
	return `${place.row}:${place.col}`;
}

/**
 * A cell's text read as a figure — spaces between its thousands, a point or a comma before its fraction — and how many
 * digits that fraction has; undefined for a text that is no figure.
 * @param {string} text
 */
function figureOf(text) {
	let plain = text.replace(/[\s  ]/g, '');
	plain = plain.includes('.') ? plain.replaceAll(',', '') : plain.replace(',', '.');
	if (!/^[-+]?\d+(\.\d+)?$/.test(plain))
		return undefined;
	return { value: Number(plain), digits: plain.split('.')[1]?.length ?? 0 };
}

/** A cell of the table copied; a figure carries its value, so a spreadsheet program reads it as a number. */
function htmlCell(/** @type {string} */ text) {
	const escaped = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
	const figure = figureOf(text.trim());
	return figure === undefined ? `<td>${escaped}</td>` : `<td x:num="${figure.value}">${escaped}</td>`;
}
