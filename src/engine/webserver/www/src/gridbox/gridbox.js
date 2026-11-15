// A SPREADSHEET (Gridbox) — a form's sheet: a report's result, a document's printed form. Its State says which sheet
// is on show by its Version; a new one comes in the same State (Sheet, ReadOnly) or is fetched whole. A report's
// gridbox also says what its sheet is to the report (ComposeState): composing — the old sheet stays on show under
// the word until the new one is delivered; outdated — the settings changed, and it waits for the person to compose.
// A double click on a cell is the report's to answer (Cell {Row, Col}, from 1) — a drill-down, a document opened.
// Cells are taken, copied and summed as in a spreadsheet program (selection.js). The sheet is filled out with empty
// cells to its frame, and on as it is scrolled to its edge — the thin client's endless sheet (sheet.js, growSheet).
//
// A sheet not ReadOnly is edited as the thin client's: a cell's text typed in place (cellEditor.js); Delete clears the
// cells taken, their text and their look; Ctrl+V pastes a text from the current cell, a line a row, a tab a column.
// Each change goes back whole (Change {Sheet}) and is the server's sheet from then on — its Version stays.

import { Control, EventKind, defineControl } from '../controls/control.js';
import { CommandBar } from '../controls/commandBar.js';
import { Command } from '../docView/tabDocument.js';
import { _ } from '../localization.js';
import { drawSheet, growSheet, setCellText } from './sheet.js';
import { CellSelection } from './selection.js';
import { CellEditor } from './cellEditor.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What a sheet is to its report (ibSpreadsheet::ibComposeState).
const ComposeState = Object.freeze({ Composed: 0, Composing: 1, Outdated: 2 });

class Gridbox extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.bar = new CommandBar(this);
		this.host = document.createElement('div');
		this.host.className = 'sheet-host';
		this.composing = document.createElement('div');
		this.composing.className = 'compose-state';
		this.composing.hidden = true;
		const summary = document.createElement('div');
		summary.className = 'selection-summary';
		summary.hidden = true;
		this.selection = new CellSelection(this.host, summary);
		this.editor = new CellEditor(this.host, this.selection, (cell, text) => this.typed(cell, text));
		this.host.addEventListener('keydown', (event) => this.key(event));
		this.host.addEventListener('paste', (event) => {
			if (event.target === this.host && this.paste(event.clipboardData?.getData('text/plain') ?? ''))
				event.preventDefault();
		});
		this.host.addEventListener('edit', (event) => this.edit(/** @type {CustomEvent} */ (event)));
		this.append(this.bar.element, this.host, this.composing, summary);
		/** @type {any} the sheet on show, as the server's node has it */
		this.sheet = {};
		/** @type {HTMLTableElement | undefined} …drawn */
		this.table = undefined;
		this.gridLines = false;
		this.readOnly = true;
		// Filled out to the frame as it — or the sheet — is sized, and half a frame past what is seen as it is scrolled on.
		this.resized = new ResizeObserver(() => this.fill(0));
		this.resized.observe(this.host);
		this.host.addEventListener('scroll', () => this.fill(0.5), { passive: true });
		/** @type {number | undefined} the Version of the sheet on show */
		this.version = undefined;
		/** whether a sheet was asked for at all — a document's tab says no Version, and its sheet is read once */
		this.read = false;
		this.composeState = ComposeState.Composed;
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.bar.update(this.state(node).CommandBar);
		return true;
	}

	/**
	 * No children: the sheet follows the State's Version, the word over it its ComposeState.
	 * @param {ProtocolNode} node
	 */
	drawChildren(node) {
		const state = this.state(node);
		this.showComposeState(state.ComposeState ?? ComposeState.Composed);
		if (this.read && state.Version === this.version)
			return;
		this.read = true;
		this.version = state.Version;
		if (state.Sheet !== undefined)
			this.draw(state.Sheet, state.GridLines === true, state.ReadOnly === true);
		else
			this.drawing = this.load(state.Version, state.GridLines === true);
	}

	/**
	 * The sheet read whole, unless another came while it was on its way.
	 * @param {number | undefined} version
	 * @param {boolean} gridLines
	 */
	async load(version, gridLines) {
		try {
			const answer = await this.context.fetch(this.node.ControlId, {});
			if (version === this.version && answer?.Sheet !== undefined)
				this.draw(answer.Sheet, gridLines, answer.ReadOnly === true);
		}
		catch {
			// The form went, or the server refused: the sheet on show stays.
		}
	}

	/**
	 * A sheet on show, nothing taken on it but its first cell.
	 * @param {any} sheet
	 * @param {boolean} gridLines
	 * @param {boolean} readOnly
	 */
	draw(sheet, gridLines, readOnly) {
		// One to be edited is the box's own copy: the frame keeps the node as the server sent it.
		this.sheet = readOnly ? sheet : structuredClone(sheet);
		this.gridLines = gridLines;
		this.readOnly = readOnly;
		this.editor.editable = !readOnly;
		this.drawTable();
		this.selection.attach(/** @type {HTMLTableElement} */ (this.table));
	}

	/** The sheet drawn again from what it holds now, after a change of more than a text — the view and what is taken kept. */
	redraw() {
		const { anchor, focus, kept } = this.selection;
		this.drawTable();
		this.selection.attach(/** @type {HTMLTableElement} */ (this.table));
		this.selection.take(anchor, focus, kept);
	}

	/** The table drawn from the sheet held, filled out to the frame where it is scrolled to. */
	drawTable() {
		this.editor.close(false);
		const { scrollLeft, scrollTop, clientWidth, clientHeight } = this.host;
		const table = drawSheet(this.sheet, {
			gridLines: this.gridLines,
			onCell: (row, col) => this.send(EventKind.Cell, { Row: row, Col: col }),
		});
		// An editable sheet's current cell is framed wider, as the thin client's.
		table.classList.toggle('editable', !this.readOnly);
		this.host.replaceChildren(table);
		// Filled out again as it narrows too — a fold of columns closed takes its width away.
		if (this.table !== undefined)
			this.resized.unobserve(this.table);
		this.resized.observe(table);
		this.table = table;
		growSheet(table, scrollLeft + clientWidth, scrollTop + clientHeight, { gridLines: this.gridLines });
		this.host.scrollLeft = scrollLeft;
		this.host.scrollTop = scrollTop;
	}

	/**
	 * A cell's text typed: into the cell where it stands, and into the sheet, which goes back.
	 * @param {HTMLTableCellElement} cell
	 * @param {string} text
	 */
	typed(cell, text) {
		setCellText(cell, text);
		this.cellAt(Number(cell.dataset.row), Number(cell.dataset.col)).value = text;
		this.changed();
	}

	/** Delete: the cells taken cleared, their text and their look (ibGridEditor::OnDelete). @param {KeyboardEvent} event */
	key(event) {
		if (event.target !== this.host || this.readOnly || event.key !== 'Delete' || event.ctrlKey || event.metaKey
			|| event.altKey || event.shiftKey)
			return;
		const blocks = this.selection.blocks();
		if (blocks.length === 0)
			return;
		event.preventDefault();
		const taken = (/** @type {any} */ cell) => blocks.some((block) => cell.row >= block.top && cell.row <= block.bottom
			&& cell.col >= block.left && cell.col <= block.right);
		this.sheet.cells = (this.sheet.cells ?? []).filter((/** @type {any} */ cell) => !taken(cell));
		this.redraw();
		this.changed();
	}

	/**
	 * An edit of the Edit menu, as the thin client's sheet answers one (ibGridEditor's wxID_COPY, wxID_PASTE,
	 * wxID_SELECTALL): its own copy, paste and select-all; Cut and Delete are not a sheet's.
	 * @param {CustomEvent} event (editFocused) {command, text}
	 */
	edit(event) {
		const { command, text } = event.detail;
		if (event.target !== this.host)
			return;
		if (command === Command.Copy)
			this.selection.copy();
		else if (command === Command.SelectAll)
			this.selection.selectAll();
		else if (command !== Command.Paste || !this.paste(text))
			return;
		event.preventDefault();
	}

	/**
	 * Edit → Find over the sheet (window.js, findOnShow): the next cell holding the text becomes the current one
	 * (CellSelection.find).
	 * @param {import('../window/find.js').Asked} asked
	 */
	find(asked) {
		return this.selection.find(asked);
	}

	/**
	 * Ctrl+V, Edit → Paste: a text pasted from the current cell on — a line a row, a tab a column (ibGridEditor::Paste,
	 * a text's). False when the sheet takes none: read-only, no current cell, no text.
	 * @param {string} text
	 */
	paste(text) {
		const from = this.selection.anchor;
		if (this.readOnly || from === null || text === '')
			return false;
		const cells = (this.sheet.cells ??= []);
		/** @type {Map<string, any>} */
		const byPlace = new Map(cells.map((/** @type {any} */ cell) => [`${cell.row}:${cell.col}`, cell]));
		text.replace(/\r?\n$/, '').split(/\r?\n/).forEach((line, down) => line.split('\t').forEach((field, across) => {
			const row = from.row + down;
			const col = from.col + across;
			const cell = byPlace.get(`${row}:${col}`);
			if (cell !== undefined)
				cell.value = field;
			else
				cells.push({ row, col, value: field });
		}));
		this.redraw();
		this.changed();
		return true;
	}

	/**
	 * The sheet's cell at a place, made when it has none.
	 * @param {number} row
	 * @param {number} col
	 */
	cellAt(row, col) {
		const cells = (this.sheet.cells ??= []);
		let cell = cells.find((/** @type {any} */ one) => one.row === row && one.col === col);
		if (cell === undefined) {
			cell = { row, col };
			cells.push(cell);
		}
		return cell;
	}

	/** The sheet as the person left it, handed over whole: the server's from now on. */
	changed() {
		this.send(EventKind.Change, { Sheet: this.sheet });
	}

	/**
	 * The sheet filled out to what its frame shows and a part of a frame past it.
	 * @param {number} ahead how much of a frame past what is seen
	 */
	fill(ahead) {
		if (this.table === undefined)
			return;
		const { scrollLeft, scrollTop, clientWidth, clientHeight } = this.host;
		if (growSheet(this.table, scrollLeft + clientWidth * (1 + ahead), scrollTop + clientHeight * (1 + ahead),
			{ gridLines: this.gridLines }))
			this.selection.grown();
	}

	dispose() {
		this.resized.disconnect();
	}

	/** @param {number} state */
	showComposeState(state) {
		if (state === this.composeState)
			return;
		this.composeState = state;
		this.composing.hidden = state === ComposeState.Composed;
		this.composing.replaceChildren();
		if (state === ComposeState.Composing)
			this.composing.append(document.createElement('wa-spinner'));
		this.composing.append(state === ComposeState.Composing ? _('Composing...')
			: _('The settings have changed. Press Compose to build the report.'));
	}
}

defineControl('Gridbox', Gridbox);
