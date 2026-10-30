// A TABLE (Tablebox) — its command bar, its columns (the node's children: TableboxColumn, and a column group's own),
// and its rows, read by pages from the server and never all at once: a list is paged in the engine, and pretending
// otherwise on the way to the browser would mean reading a whole catalogue to draw twenty lines of it. The grid is
// Tabulator (vendor/tabulator), loaded only when a form has a table on it, and on a narrow screen it folds the columns
// that do not fit into the row they belong to. The thin client's table (frmclient/visualView/ctrl/tableBox.cpp) is
// the one this follows.
//
// The first page comes in the State (Rows, protocol 5) or is fetched — a reset with no anchor, which the server reads
// from the top, or around the row it stands on with as many rows ahead as the page, the page saying how many came
// (Before); the next ones are fetched as the person scrolls — after the last row held (Direction 1) and before the
// first (Direction -1), keyset, never an offset.
// A new Version is the rows read anew IN PLACE: around the row at the top of the window, which stays where it stood —
// from the State's page when it holds that row, else fetched. A read is one of a generation: a page asked before the
// rows were read anew is dropped when it comes.
//
// ⭐ A WINDOW OF ROWS, NOT ALL OF THEM — the thin client's (frmclient/win/ctrls/dataview, datavgen.paged.cpp): a page is
// as many rows as the window shows, asked when fewer than `slack` rows are left past its edge the way the person goes,
// and the rows held are kept to the window and `slack` on each side — the side left behind let go, never a row on view
// nor the current one. Read to the end of a long list, the browser holds what it shows, not the list. So the grid's own
// scrollbar would measure nothing; the thin client's stands beside it instead (updatePager): three places — at the
// top, in the middle, at the bottom — from whether there are rows before and after the window, not from a length
// nobody knows.
//
// What the person does goes back as the thin client's table sends it: the cursor moved (Row, held until the next call),
// a row activated (Open {Row}) — its cell's editor opened at once when the row has one that may be edited, else the one
// the server opens (State's Edit) — a sortable header clicked (Sort {Column}), a column's edge dragged (Resize, held), a
// cell typed into (Input) and committed (Change {Row, Column, Text}), an editor's button (Select, Open, Clear {Row,
// Column}). The cell shows the server's text, never the one typed: what the server takes comes back with the rows read
// anew, and an edit it refused leaves the cell as it was.
//
// HOW IT SHOWS ITS ROWS is the State's ViewMode, the person's to switch (the bar's View mode, a question of the server's):
// every row flat (List); one level, a folder activated entered and the folders it stands in frozen over its rows, one of
// them activated gone back out of (Hierarchical, the thin client's crumbs); or the hierarchy in place, a folder opened
// by its expander (Tree). Every read names its level (Parent). A tree's rows opened stay held: only the top level is
// paged, and what the window lets go of applies to a flat level alone.
//
// WHAT THE CURSOR STANDS ON is the table's RowSelectionMode: the row, or the cell, its row tinted (the thin client's). A
// second click on the cell it stands on opens the cell's editor, and the keys are the data view's (pressKey).
//
// THE FIRST COLUMN'S CELL is the row's as much as its own, as the thin client draws it: the step in, the expander, the
// row's picture, what the cell shows (firstCell). A row with a Group is that caption alone, across the whole line.

import { Control, EventKind, defineControl } from '../controls/control.js';
import { CommandBar } from '../controls/commandBar.js';
import { TextEditor } from '../controls/textEditor.js';
import { readPage } from './rows.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('tabulator').TabulatorFull} TabulatorFull */

/** The rows held past the window on each side (the thin client's kBufferSlack). */
const slack = 5;

// Which way a fetch reads from its Anchor (ibTable::ibDirection).
const backward = -1;
const reset = 0;
const forward = 1;

// A header's alignment (wx's flags).
const centred = 0x100;
const right = 0x200;

// What a column's header shows (ibState::ibRepresentation): its picture alone.
const pictureOnly = 2;

// How a column group lays its columns (ibColumnGroupKind): side by side, one under the other in one column, in one cell.
const sideBySide = 0;
const oneUnderAnother = 1;

// A command that edits (eStartEditingFlag): the current row's editor is opened before it goes.
const startEditing = 0x1000;

// How a table shows its rows (ibTable::ibViewMode).
const treeView = 0;
const hierarchicalView = 1;

// What the cursor stands on (ibDataViewSelectionMode): the cell; otherwise its row.
const selectCell = 0;

/** A tree's step in, per level, in pixels — and how many of a folder's rows one read asks for. */
const treeIndent = 16;
const treeBatch = 100;

/** How long a second click on the cursor's cell waits for the double click it may be (the data view's rename timer). */
const secondClickDelay = 100;

/** @type {Promise<typeof import('tabulator').TabulatorFull> | null} */
let loading = null;

/** The grid, and its stylesheet, fetched once — and only for a form that has a table. */
function loadGrid() {
	if (loading === null) {
		const sheet = document.createElement('link');
		sheet.rel = 'stylesheet';
		sheet.href = new URL('../../vendor/tabulator/6.6.1/tabulator.min.css', import.meta.url).href;
		document.head.append(sheet);
		loading = import('tabulator').then((module) => module.TabulatorFull);
	}
	return loading;
}

class Tablebox extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.bar = new CommandBar(this);
		// The grid, and beside it the scrollbar over the window of rows it holds.
		this.frame = document.createElement('div');
		this.frame.className = 'grid-frame';
		this.host = document.createElement('div');
		this.host.className = 'grid';
		this.pager = document.createElement('div');
		this.pager.className = 'pager';
		this.thumb = document.createElement('div');
		this.thumb.className = 'thumb';
		this.pager.append(this.thumb);
		this.frame.append(this.host, this.pager);
		this.append(this.bar.element, this.frame);
		/** @type {TabulatorFull | null} */
		this.table = null;
		/** @type {Promise<TabulatorFull> | null} */
		this.built = null;
		this.disposed = false;
		this.drawnColumns = '';
		/** @type {number[]} what stands directly under the table, as the server has it, the hidden columns too */
		this.columnIds = [];
		/** @type {number | undefined} the Version of the rows held */
		this.version = undefined;
		/** the reads of the rows held — a page asked under an older one is dropped */
		this.generation = 0;
		/** the rows held reach the top, the bottom */
		this.begin = true;
		this.end = true;
		this.firstHandle = 0;
		this.lastHandle = 0;
		this.fetchingAhead = false;
		this.fetchingBack = false;
		/** where the window stood at the last scroll — which way the person goes */
		this.scrolled = 0;
		/** the row and the column the cursor stands on */
		this.currentRow = 0;
		this.currentColumn = 0;
		/** the column under the last press — one of a group's drawn in one grid column */
		this.pressedColumn = 0;
		/** the last press was inside a cell's editor: the editor's, no move of the cursor */
		this.pressedEditor = false;
		/** the table had the focus as the last press came: a click on the cursor's cell is then a second one */
		this.pressedFocused = false;
		/** @type {ReturnType<typeof setTimeout> | undefined} a second click, waiting to open the editor */
		this.secondClick = undefined;
		/** the keys put the current column: on a row selected whole, its cell is marked */
		this.columnByKeys = false;
		/** @type {number | undefined} how its rows are shown (the State's ViewMode) */
		this.viewMode = undefined;
		/** @type {any[]} the folders it stands in, the outermost first — the Hierarchical view's records of them */
		this.crumbs = [];
		/** the cursor stands on a cell, not on a row */
		this.cellMode = false;
		/** @type {Element | null} the row and the cell marked as the cursor's */
		this.markedRow = null;
		this.markedCell = null;
		/** the first rows are in the grid: {@link Control#drawing} */
		this.drawn = () => {};
		this.drawing = new Promise((resolve) => { this.drawn = resolve; });
		/** a row activated with no cell the client may edit: the editor the server opens is opened here */
		this.awaitEdit = false;
		/** @type {ProtocolNode | undefined} the table's standard commands, as its State offers them */
		this.contextMenu = undefined;
		this.host.addEventListener('pointerdown', (event) => {
			this.pressedColumn = columnUnder(event.target);
			this.pressedEditor = event.target instanceof Element && event.target.closest('.cell-editor') !== null;
			this.pressedFocused = this.host.contains(document.activeElement);
		}, true);
		// A press on a row leaves the focus on the table: on a cell it would go with the cell, which the grid draws anew.
		this.host.addEventListener('mousedown', (event) => {
			if (event.target instanceof Element && event.target.closest('.tabulator-row') !== null
				&& event.target.closest('.cell-editor') === null) {
				event.preventDefault();
				this.host.focus({ preventScroll: true });
			}
		});
		this.listenPager();
		// A window grown taller shows more rows than are held past it: read on, and the scrollbar placed again.
		this.resized = new ResizeObserver(() => {
			this.updatePager();
			this.fill();
		});
		this.resized.observe(this.frame);
		this.listenKeys();
		this.update(node);
	}

	/**
	 * The focus on the rows: the keys are theirs.
	 * @param {FocusOptions} [options]
	 */
	focus(options) {
		this.host.focus(options);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.bar.update(this.state(node).CommandBar);
		this.contextMenu = this.state(node).ContextMenu;
		this.host.classList.toggle('no-header', node.Header === false);
		this.host.style.setProperty('--oes-header-lines', String(node.HeaderHeight ?? 1));
		this.host.style.setProperty('--oes-footer-lines', String(node.FooterHeight ?? 1));
		this.cellMode = node.RowSelectionMode === selectCell;
		this.host.classList.toggle('select-cell', this.cellMode);
		return true;
	}

	/**
	 * What the person did, sent — and a command of the table's (its bar's, its context menu's) that edits opens the
	 * current row's editor before it goes, or awaits the one the server opens: the thin client's CallAsAction.
	 * @param {number} kind
	 * @param {any} [args]
	 */
	send(kind, args) {
		if ((kind === EventKind.Command || kind === EventKind.Action) && ((args?.Id ?? 0) & startEditing) !== 0
			&& !this.editCurrentRow())
			this.awaitEdit = true;
		return super.send(kind, args);
	}

	/**
	 * The columns are a header, and the rows follow the State's Version.
	 * @param {ProtocolNode} node
	 */
	drawChildren(node) {
		const state = this.state(node);
		const mode = state.ViewMode ?? hierarchicalView;
		const switched = this.viewMode !== undefined && this.viewMode !== mode;
		this.viewMode = mode;
		this.columnIds = (node.NodeChildren ?? []).map((child) => child.ControlId);
		const definitions = (node.NodeChildren ?? []).flatMap((child) => this.definitionsOf(child, node));
		const first = firstShown(definitions);
		if (first !== undefined)
			first.formatterParams = { ...first.formatterParams, first: true };
		// The columns frozen at the left (its FrezeeCol).
		const frozen = node.FrezeeCol ?? 0;
		if (frozen > 0)
			definitions.slice(0, frozen).forEach((definition) => { definition.frozen = true; });
		const drawn = JSON.stringify(definitions, (name, value) => typeof value === 'function' ? undefined : value);
		this.grid(definitions, node).then((table) => {
			if (this.disposed)
				return;
			if (drawn !== this.drawnColumns) {
				this.drawnColumns = drawn;
				table.setColumns(definitions);
			}
			if (state.CurrentColumn)
				this.currentColumn = state.CurrentColumn;
			if (switched) {
				// Another way of showing them: read from nothing, no folder stood in. The State's page is of the mode just
				// left unless its Version moved with the switch.
				this.crumbs = [];
				const moved = state.Version !== this.version;
				this.version = state.Version;
				this.load(moved ? state.Rows : undefined, state.CurrentRow ?? 0);
			}
			else if (state.Version !== this.version) {
				const first = this.version === undefined;
				this.version = state.Version;
				if (first)
					this.load(state.Rows, state.CurrentRow ?? 0);
				else
					this.reload(state.Rows);
			}
			else if (state.CurrentRow)
				this.select(state.CurrentRow);
			else
				this.markCursor();
			if (this.awaitEdit && state.Edit !== undefined) {
				this.awaitEdit = false;
				const column = state.Edit.Column;
				const cell = this.rowOf(state.Edit.Row)?.getCells().find((one) => columnsOf(one).includes(column));
				if (cell !== undefined)
					this.editIn(cell, column);
			}
		});
	}

	/**
	 * The grid, built once with its first columns — and the rows frozen at its top (its FrezeeRow), which the grid
	 * takes only as it is built.
	 * @param {object[]} definitions
	 * @param {ProtocolNode} node
	 * @returns {Promise<TabulatorFull>}
	 */
	grid(definitions, node) {
		if (this.built !== null)
			return this.built;
		this.drawnColumns = JSON.stringify(definitions, (name, value) => typeof value === 'function' ? undefined : value);
		this.built = loadGrid().then((Grid) => new Promise((resolve) => {
			const table = new Grid(this.host, {
				index: 'handle',
				height: '100%',
				layout: 'fitColumns',
				responsiveLayout: 'collapse',
				placeholder: '',
				// The keys are the table's own (pressKey), not the grid's scrolling.
				keybindings: false,
				// A column dragged by its header, where the columns are a row of them: a group holds its own.
				movableColumns: (node.NodeChildren ?? []).every((child) => child.NodeType === 'TableboxColumn'
					&& child.Reorderable !== false),
				// A row the grid draws as the window comes to it: the cursor, a crumb and a group's caption are marked as it is.
				rowFormatter: (row) => {
					const record = row.getData();
					row.getElement().classList.toggle('crumb', record.crumb === true);
					row.getElement().classList.toggle('group-row', Boolean(record.group));
					// Tab is the table's (pressKey): a cell with an editor is no stop of the page's.
					for (const cell of row.getElement().querySelectorAll('.tabulator-cell[tabindex]'))
						cell.removeAttribute('tabindex');
					if (record.handle === this.currentRow)
						this.markCursor();
				},
				// A cell's editor opens as the row is activated, as the desktop's does — a click only moves the cursor.
				editTriggerEvent: 'dblclick',
				frozenRows: node.FrezeeRow > 0 ? node.FrezeeRow : 0,
				columns: definitions,
				data: [],
			});
			table.on('tableBuilt', () => {
				// Taken away while it was built: nothing is drawn into a form gone.
				if (this.disposed) {
					table.destroy();
					return;
				}
				this.table = table;
				this.listen(table);
				resolve(table);
			});
		}));
		return this.built;
	}

	/** @param {TabulatorFull} table */
	listen(table) {
		table.on('cellClick', (event, cell) => {
			if (this.pressedEditor || cell.getRow().getData().crumb === true)
				return;
			const column = this.pressedColumn || idOf(cell.getField());
			const second = this.pressedFocused && cell.getRow().getData().handle === this.currentRow && column === this.currentColumn;
			this.sendCursor(cell);
			// The file manager's rule, the data view's: a click on the cell the cursor stands on opens its editor.
			if (second && mayEdit(cell, column))
				this.secondClick = setTimeout(() => this.editIn(cell, column), secondClickDelay);
		});
		table.on('cellDblClick', (event, cell) => {
			clearTimeout(this.secondClick);
			if (!this.pressedEditor)
				this.activate(cell);
		});
		table.on('columnMoved', (column, columns) => this.sendMove(column, columns));
		table.on('rowContext', (event, row) => {
			// The desktop's OnContextMenu: the table's standard commands, as the server offers them.
			const entries = this.contextMenu?.NodeChildren ?? [];
			if (entries.length === 0)
				return;
			event.preventDefault();
			const cell = row.getCells().find((one) => one.getElement().contains(/** @type {Node} */ (event.target)));
			if (cell !== undefined)
				this.sendCursor(cell);
			this.showContextMenu(entries, event.clientX, event.clientY);
		});
		table.on('columnResized', (column) => {
			const id = idOf(column.getField());
			if (id !== 0)
				this.send(EventKind.Resize, { Column: id, Width: Math.round(column.getWidth()) });
		});
		table.on('scrollVertical', (top) => {
			const direction = Math.sign(top - this.scrolled);
			this.scrolled = top;
			this.edge(direction);
			this.updatePager();
		});
	}

	/**
	 * A column dragged by its header, told where it now stands among the table's columns, the hidden ones too.
	 * @param {any} column
	 * @param {any[]} columns as they stand now
	 */
	sendMove(column, columns) {
		const id = idOf(column.getField());
		const order = columns.map((one) => idOf(one.getField()));
		const next = order[order.indexOf(id) + 1];
		const others = this.columnIds.filter((one) => one !== id);
		this.send(EventKind.Move, { Column: id, Holder: 0, Position: next === undefined ? others.length : others.indexOf(next) });
	}

	/**
	 * A row double-clicked. In the Hierarchical view a folder is entered and a crumb gone back out of — the view's own,
	 * nothing for the server to open (the thin client's SetTopParent). Anything else is the row activated; the cell clicked
	 * opens its editor when it may be edited (the grid opens it).
	 * @param {any} cell
	 */
	activate(cell) {
		const record = cell.getRow().getData();
		if (record.crumb === true) {
			this.leave(record.handle);
			return;
		}
		if (this.viewMode === hierarchicalView && record.container) {
			this.enter(record);
			return;
		}
		if (mayEdit(cell, this.pressedColumn))
			this.send(EventKind.Open, { Row: record.handle });
		else
			this.activateRow(record.handle);
	}

	/**
	 * The desktop's ActivateRow, a double click's and Enter's: the row's current or first cell that may be edited opens its
	 * editor, and with none the server opens one if it will. What else activating it is the server decides — the row's
	 * object opened, a choice made.
	 * @param {number} handle
	 */
	activateRow(handle) {
		if (!this.editCurrentRow())
			this.awaitEdit = true;
		this.send(EventKind.Open, { Row: handle });
	}

	/**
	 * A folder entered: its level read, it frozen over its rows.
	 * @param {any} record
	 */
	enter(record) {
		return this.readLevel([...this.crumbs, { ...record, crumb: true }]);
	}

	/**
	 * A crumb activated: it and what stands in it gone, the level it stands in read.
	 * @param {number} handle
	 */
	leave(handle) {
		return this.readLevel(this.crumbs.slice(0, this.crumbs.findIndex((crumb) => crumb.handle === handle)));
	}

	/**
	 * The level the crumbs lead to, read from its top; the cursor stands on the row the server names, else on its first.
	 * A level refused stays unentered.
	 * @param {any[]} crumbs
	 */
	async readLevel(crumbs) {
		if (this.table === null)
			return;
		const generation = ++this.generation;
		const parent = crumbs.at(-1)?.handle ?? 0;
		const page = await this.fetch({ Direction: reset, Parent: parent, Count: this.perPage() + slack, Before: 0 });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		this.crumbs = crumbs;
		const read = readPage(page);
		await this.setRows(read.records);
		this.take(read);
		this.scrollTo(0);
		if (read.current !== 0)
			this.select(read.current, false);
		else if (read.records.length > 0)
			this.moveCursor(read.records[0].handle, this.currentColumn);
		this.updatePager();
		this.fill();
	}

	/**
	 * The rows of a read anew, the crumbs frozen over them.
	 * @param {any[]} records
	 * @param {boolean} [inPlace] the window stays where it stands
	 */
	async setRows(records, inPlace = false) {
		if (this.table === null)
			return;
		await (inPlace ? this.table.replaceData(records) : this.table.setData(records));
		if (this.crumbs.length > 0) {
			// A copy: the grid turns the rows it adds at the top round in place.
			await this.table.addData(this.crumbs.map((crumb) => ({ ...crumb })), true);
			for (const crumb of this.crumbs)
				this.rowOf(crumb.handle)?.freeze();
		}
	}

	/**
	 * A folder of the tree opened or closed by its expander.
	 * @param {any} row
	 */
	async toggle(row) {
		const record = row.getData();
		if (record.open)
			await this.collapse(row);
		else
			await this.expand(row);
		this.updatePager();
	}

	/**
	 * A folder of the tree opened: its rows read whole — after it, a level further in — and those of them that were open
	 * before a read anew opened again.
	 * @param {any} row
	 * @param {Set<string>} [wasOpen]
	 * @returns {Promise<any[]>} what it held
	 */
	async expand(row, wasOpen) {
		const record = row.getData();
		const generation = this.generation;
		const records = await this.readFolder(record);
		if (records === null || generation !== this.generation || this.table === null)
			return [];
		record.open = true;
		row.reformat();
		await this.table.addData(records.map((one) => ({ ...one })), false, row);
		if (wasOpen !== undefined)
			await this.reopen(records, wasOpen);
		this.markCursor();
		return records;
	}

	/**
	 * A folder of the tree opened with every folder in it (the keys' *).
	 * @param {any} row
	 */
	async expandWhole(row) {
		for (const record of await this.expand(row)) {
			const inner = record.container ? this.rowOf(record.handle) : null;
			if (inner !== null)
				await this.expandWhole(inner);
		}
	}

	/**
	 * A folder's rows, all of them — read ahead of the page too when the server stands inside it. Null when refused.
	 * @param {any} folder
	 */
	async readFolder(folder) {
		const level = (record) => ({ ...record, level: folder.level + 1, path: `${folder.path}/${keyOf(record)}` });
		const page = await this.fetch({ Direction: reset, Parent: folder.handle, Count: treeBatch, Before: 0 });
		if (page === null)
			return null;
		const read = readPage(page);
		let records = read.records;
		for (let end = read.end; !end && records.length > 0;) {
			const next = await this.fetch({ Direction: forward, Parent: folder.handle, Anchor: records.at(-1).handle, Count: treeBatch });
			if (next === null)
				return null;
			const more = readPage(next);
			records = [...records, ...more.records];
			end = more.end || more.records.length < treeBatch;
		}
		for (let begin = read.begin; !begin && records.length > 0;) {
			const next = await this.fetch({ Direction: backward, Parent: folder.handle, Anchor: records[0].handle, Count: treeBatch });
			if (next === null)
				return null;
			const more = readPage(next);
			records = [...more.records, ...records];
			begin = more.records.length < treeBatch;
		}
		return records.map(level);
	}

	/**
	 * A folder of the tree closed: what stands in it let go of, the cursor moved onto it if it stood there.
	 * @param {any} row
	 */
	async collapse(row) {
		const record = row.getData();
		const rows = this.table?.getRows() ?? [];
		const inside = [];
		for (let at = rows.indexOf(row) + 1; at < rows.length && rows[at].getData().level > record.level; at++)
			inside.push(rows[at]);
		const cursorInside = inside.some((one) => one.getData().handle === this.currentRow);
		if (inside.length > 0)
			await this.table?.deleteRow(inside);
		record.open = false;
		row.reformat();
		if (cursorInside)
			this.moveCursor(record.handle, this.currentColumn);
		else
			this.markCursor();
	}

	/**
	 * The folders open before a read anew, opened again — a row read again has a new handle, so they are known by the way
	 * to them: what their rows read.
	 * @param {any[]} records
	 * @param {Set<string>} wasOpen
	 */
	async reopen(records, wasOpen) {
		for (const record of records) {
			if (record.container && wasOpen.has(record.path)) {
				const row = this.rowOf(record.handle);
				if (row !== null)
					await this.expand(row, wasOpen);
			}
		}
	}

	/** The keys of the rows — wherever in the grid the focus is (a click leaves it on the grid's window), but an editor. */
	listenKeys() {
		this.host.tabIndex = 0;
		this.host.addEventListener('keydown', (event) => {
			if (this.table === null || event.ctrlKey || event.altKey || event.metaKey
				|| (event.target instanceof Element && event.target.closest('.cell-editor') !== null))
				return;
			if (this.pressKey(event.key, event.shiftKey))
				event.preventDefault();
		});
	}

	/**
	 * A key among the rows, the data view's (ProcessTableCharEvent): the arrows, a page, the ends; sideways and Tab a
	 * column, in a tree a folder closed, opened, its parent; + - * a folder; Enter activates; F2 and Space edit.
	 * @param {string} key
	 * @param {boolean} shift
	 * @returns {boolean} it was one of its
	 */
	pressKey(key, shift) {
		const rows = this.heldRows();
		const at = rows.findIndex((row) => row.getData().handle === this.currentRow);
		switch (key) {
		case 'ArrowUp': return this.goToRow(rows, at - 1);
		case 'ArrowDown': return this.goToRow(rows, at + 1);
		case 'PageUp': return this.goToRow(rows, at - (this.perPage() - 1));
		case 'PageDown': return this.goToRow(rows, at + (this.perPage() - 1));
		case 'Home': return this.goToRow(rows, 0);
		case 'End': return this.goToRow(rows, rows.length - 1);
		}
		const row = rows[at];
		if (row === undefined)
			return false;
		const record = row.getData();
		const folder = this.viewMode === treeView && record.container === true;
		const columns = row.getCells().flatMap(columnsOf);
		switch (key) {
		case 'ArrowLeft':
			if (this.moveColumn(columns, -1))
				return true;
			if (folder && record.open)
				this.toggle(row);
			else
				this.goToRow(rows, rows.slice(0, at).findLastIndex((one) => one.getData().level < record.level));
			return true;
		case 'ArrowRight':
			if (!folder)
				return this.moveColumn(columns, 1);
			if (record.open)
				this.goToRow(rows, at + 1);
			else
				this.toggle(row);
			return true;
		case 'Tab':
			return this.moveColumn(columns, shift ? -1 : 1) || this.tabToRow(rows[at + (shift ? -1 : 1)], shift);
		case '+':
			if (folder && !record.open)
				this.toggle(row);
			return true;
		case '-':
			if (folder && record.open)
				this.toggle(row);
			return true;
		case '*':
			if (folder && record.open)
				this.toggle(row);
			else if (folder)
				this.expandWhole(row).then(() => this.updatePager());
			return true;
		case 'Enter':
			this.activateRow(record.handle);
			return true;
		case 'F2':
		case ' ':
			this.editCurrentRow();
			return true;
		default:
			return false;
		}
	}

	/**
	 * The cursor on a row of those held, the place kept inside them, and the row brought into view.
	 * @param {any[]} rows
	 * @param {number} at
	 */
	goToRow(rows, at) {
		const row = rows[Math.max(0, Math.min(rows.length - 1, at))];
		if (row === undefined)
			return false;
		this.moveCursor(row.getData().handle, this.currentColumn);
		row.scrollTo('nearest', false).catch(() => {});
		return true;
	}

	/**
	 * The cursor a column along its row. On a row selected whole the first press marks the first cell, and back past it the
	 * row is whole again (the data view's TryAdvanceCurrentColumn).
	 * @param {number[]} columns the row's
	 * @param {number} step
	 * @returns {boolean} false at the row's edge
	 */
	moveColumn(columns, step) {
		const place = columns.indexOf(this.currentColumn);
		const from = this.cellMode ? Math.max(0, place) : this.columnByKeys ? place : -1;
		const next = from + step;
		if (next < 0 && from === 0 && !this.cellMode) {
			this.columnByKeys = false;
			this.markCursor();
			return true;
		}
		if (next < 0 || next >= columns.length)
			return false;
		this.columnByKeys = true;
		this.moveCursor(this.currentRow, columns[next]);
		this.markCursor();
		this.markedCell?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
		return true;
	}

	/**
	 * Tab past a row's edge: the next row's first column, or the one before's last; with none the focus leaves the table.
	 * @param {any} row
	 * @param {boolean} back
	 */
	tabToRow(row, back) {
		if (row === undefined)
			return false;
		const columns = row.getCells().flatMap(columnsOf);
		this.columnByKeys = true;
		this.moveCursor(row.getData().handle, back ? columns.at(-1) : columns[0]);
		row.scrollTo('nearest', false).catch(() => {});
		return true;
	}

	/**
	 * A page read the way the person goes when fewer than `slack` rows are left past the window's edge — only that way:
	 * read the other way too, a window short of rows behind it jumped as its far side was let go (the thin client's
	 * OnScrollEvent).
	 * @param {number} direction 1 down, -1 up
	 */
	edge(direction) {
		const holder = this.holder();
		if (holder === null || holder.clientHeight === 0)
			return;
		const margin = slack * this.rowHeight();
		if (direction > 0 && holder.scrollHeight - holder.scrollTop - holder.clientHeight < margin)
			this.more();
		else if (direction < 0 && holder.scrollTop < margin)
			this.back();
	}

	/**
	 * Rows read while those held do not reach `slack` past the window — a short first page, a window grown: on, and back
	 * once there is nothing after them (a window read around the last row would otherwise hold that row alone, with no
	 * scroll to reach the rows before it). Not while the table is not on show (a notebook page behind another): with no
	 * height no number of rows would fill it, and the whole list was read; it is read when shown (the resize).
	 */
	fill() {
		const holder = this.holder();
		if (holder === null || holder.clientHeight === 0)
			return;
		if (holder.scrollHeight - holder.clientHeight >= slack * this.rowHeight())
			return;
		if (!this.end)
			this.more();
		else if (!this.begin)
			this.back();
	}

	/** The rows the window shows (the thin client's GetCountPerPage) — the size of a page. */
	perPage() {
		const holder = this.holder();
		return Math.max(1, Math.ceil((holder?.clientHeight ?? 0) / this.rowHeight()));
	}

	/** A row's height, as drawn; before any is, the touch target's. */
	rowHeight() {
		const row = /** @type {HTMLElement | null} */ (this.host.querySelector('.tabulator-row'));
		return row?.offsetHeight || 32;
	}

	/**
	 * The scrollbar over the window, the thin client's three-place one (datavgen.scrollbar.cpp): at the top with
	 * nothing before what is shown, at the bottom with nothing after it, in the middle with rows both ways; none at all
	 * when every row is held and on view. It stands over the grid's rows, under its header.
	 */
	updatePager() {
		const holder = this.holder();
		if (holder === null)
			return;
		const atTop = holder.scrollTop <= 0;
		const atBottom = holder.scrollTop + holder.clientHeight >= holder.scrollHeight - 1;
		const ahead = !this.end || !atBottom;
		const behind = !this.begin || !atTop;
		this.frame.classList.toggle('paged', ahead || behind);
		this.pager.dataset.thumb = ahead && behind ? 'middle' : behind ? 'bottom' : 'top';
		this.pager.style.top = `${holder.offsetTop}px`;
		this.pager.style.height = `${holder.clientHeight}px`;
	}

	/**
	 * What the scrollbar takes: a press beside the thumb turns a page that way, the wheel scrolls the rows, and the thumb
	 * dragged turns a page the way it went — let go at the very top, the list is read again from its first row (the
	 * thin client's snap to the start). Dragged, it follows the pointer and comes back to its place.
	 */
	listenPager() {
		/** @type {number | null} */
		let pressed = null;
		/** where the thumb rests and the track it moves in, as the drag began */
		let rest = new DOMRect();
		let track = new DOMRect();
		const page = (/** @type {number} */ direction) => {
			const holder = this.holder();
			if (holder !== null)
				holder.scrollTop += direction * holder.clientHeight;
		};
		this.pager.addEventListener('wheel', (event) => {
			const holder = this.holder();
			if (holder === null)
				return;
			event.preventDefault();
			holder.scrollTop += event.deltaY;
		}, { passive: false });
		this.pager.addEventListener('pointerdown', (event) => {
			event.preventDefault();
			if (event.target !== this.thumb) {
				const thumb = this.thumb.getBoundingClientRect();
				page(event.clientY < thumb.top ? -1 : 1);
				return;
			}
			pressed = event.clientY;
			rest = this.thumb.getBoundingClientRect();
			track = this.pager.getBoundingClientRect();
			this.thumb.setPointerCapture(event.pointerId);
		});
		this.thumb.addEventListener('pointermove', (event) => {
			if (pressed === null)
				return;
			const shift = Math.min(Math.max(event.clientY - pressed, track.top - rest.top), track.bottom - rest.bottom);
			this.thumb.style.translate = `0 ${shift}px`;
		});
		this.thumb.addEventListener('pointerup', (event) => {
			if (pressed === null)
				return;
			const shift = event.clientY - pressed;
			pressed = null;
			this.thumb.style.translate = '';
			if (rest.top + shift <= track.top && !this.begin)
				this.fromTop();
			else if (shift !== 0)
				page(Math.sign(shift));
		});
		this.thumb.addEventListener('pointercancel', () => {
			pressed = null;
			this.thumb.style.translate = '';
		});
	}

	/**
	 * The cursor moved to a cell — its row, and its column, one of a group's the one pressed — told when it moved (the
	 * thin client's SendCursor); the person going on lets an activation's editor go.
	 * @param {any} cell
	 */
	sendCursor(cell) {
		this.columnByKeys = false;
		this.moveCursor(cell.getRow().getData().handle, this.pressedColumn || idOf(cell.getField()));
		this.markCursor();
	}

	/**
	 * The cursor moved here, marked and told.
	 * @param {number} handle
	 * @param {number} column
	 */
	moveCursor(handle, column) {
		if (handle === this.currentRow && column === this.currentColumn)
			return;
		if (handle !== this.currentRow)
			this.awaitEdit = false;
		this.currentRow = handle;
		this.currentColumn = column;
		this.markCursor();
		this.send(EventKind.Row, column !== 0 ? { Row: handle, Column: column } : { Row: handle });
	}

	/**
	 * The cursor's marks where it stands: its row and, when it stands on cells or the keys put a column, its cell — the
	 * current column's, else the row's first; of a group drawn as one, that column's part.
	 */
	markCursor() {
		const row = this.rowOf(this.currentRow);
		const element = row?.getElement() ?? null;
		if (this.markedRow !== element) {
			this.markedRow?.classList.remove('current-row');
			element?.classList.add('current-row');
			this.markedRow = element;
		}
		/** @type {Element | null} */
		let cell = null;
		if ((this.cellMode || this.columnByKeys) && row !== null) {
			const cells = row.getCells();
			const found = cells.find((one) => columnsOf(one).includes(this.currentColumn)) ?? cells[0];
			cell = found?.getElement().querySelector(`[data-column="${this.currentColumn}"]`) ?? found?.getElement() ?? null;
		}
		if (this.markedCell !== cell) {
			this.markedCell?.classList.remove('current-cell');
			cell?.classList.add('current-cell');
			this.markedCell = cell;
		}
	}

	/**
	 * The current row's editor opened — in the current column when its cell may be edited, else the first that may
	 * (the desktop's EditCurrentRow); false when none may.
	 */
	editCurrentRow() {
		const row = this.rowOf(this.currentRow);
		if (row === null)
			return false;
		const cells = row.getCells();
		const current = cells.find((cell) => columnsOf(cell).includes(this.currentColumn)
			&& mayEdit(cell, this.currentColumn));
		if (current !== undefined) {
			this.editIn(current, this.currentColumn);
			return true;
		}
		for (const cell of cells) {
			const column = columnsOf(cell).find((one) => mayEdit(cell, one));
			if (column !== undefined) {
				this.editIn(cell, column);
				return true;
			}
		}
		return false;
	}

	/**
	 * A cell's editor opened on one of the columns it draws.
	 * @param {any} cell
	 * @param {number} column
	 */
	editIn(cell, column) {
		this.pressedColumn = column;
		cell.edit(true);
	}

	/**
	 * The table's context menu where the person pressed, kept inside the window: an entry's picture and caption, one
	 * greyed there greyed here; the one picked goes back as Action {Id}.
	 * @param {ProtocolNode[]} entries
	 * @param {number} x
	 * @param {number} y
	 */
	showContextMenu(entries, x, y) {
		const menu = document.createElement('div');
		menu.className = 'popup-menu';
		menu.popover = 'auto';
		for (const entry of entries) {
			const item = document.createElement('button');
			item.type = 'button';
			item.disabled = entry.Enabled === false;
			const picture = this.context.picture(entry.Picture);
			if (picture) {
				const image = document.createElement('img');
				image.src = picture;
				image.alt = '';
				item.append(image);
			}
			item.append(entry.Caption ?? '');
			item.addEventListener('click', () => {
				menu.hidePopover();
				this.send(EventKind.Action, { Id: entry.Id });
			});
			menu.append(item);
		}
		menu.addEventListener('toggle', (/** @type {any} */ event) => {
			if (event.newState === 'closed')
				menu.remove();
		});
		document.body.append(menu);
		menu.showPopover();
		const size = menu.getBoundingClientRect();
		menu.style.left = `${Math.max(0, Math.min(x, innerWidth - size.width))}px`;
		menu.style.top = `${Math.max(0, Math.min(y, innerHeight - size.height))}px`;
	}

	/**
	 * A column's definition for the grid. A group lays its columns as its Grouping says: side by side — each a column
	 * of the grid — or one under the other, or in one cell — one column of the grid drawing them all; with its title
	 * shown, the group's caption stands over them. A column of the grid draws its parts (formatterParams): one for a
	 * column, a group's for a group drawn as one.
	 * @param {ProtocolNode} child
	 * @param {ProtocolNode} table
	 * @returns {object[]}
	 */
	definitionsOf(child, table) {
		const state = this.state(child);
		if (child.NodeType === 'TableboxColumnGroup') {
			if (state.Visible === false)
				return [];
			const grouping = child.Grouping ?? oneUnderAnother;
			const columns = (child.NodeChildren ?? []).flatMap((one) => this.definitionsOf(one, table))
				.filter((one) => one.visible !== false);
			const laid = grouping === sideBySide || columns.length < 2 ? columns : [this.joined(child, columns, grouping, table)];
			return child.ShowTitle !== true || laid.length === 0 ? laid
				: [{ title: state.Caption ?? '', headerHozAlign: alignOf(child.HeaderAlign ?? centred), columns: laid }];
		}
		if (child.NodeType !== 'TableboxColumn')
			return [];
		const id = child.ControlId;
		const sortable = state.Sortable === true;
		/** @type {Part} */
		const part = {
			id,
			title: state.Representation === pictureOnly ? '' : state.Caption ?? '',
			typed: state.ReadOnly !== true && child.TexteditMode !== false,
			select: child.ButtonSelect !== false,
			open: child.ButtonOpen === true,
			clear: child.ButtonClear !== false,
			password: child.PasswordMode === true,
			multiline: child.MultilineMode === true,
		};
		/** @type {any} */
		const definition = {
			title: part.title,
			field: `c${id}`,
			visible: state.Visible !== false && child.Visible !== false,
			// The saved width is the column's floor, not its width: the columns share the table's width between them,
			// as the desktop's do.
			minWidth: child.Width > 0 ? child.Width : 40,
			headerSort: false,
			headerHozAlign: alignOf(child.HeaderAlign ?? 0),
			// The header's picture and sort are in the definition, so one changed draws the header again.
			titleFormatter: (cell, header) => headerOf(this, cell.getValue(), header),
			titleFormatterParams: { picture: state.HeaderPicture ?? '', sort: state.Sort ?? 0, sortable },
			// The sort is the server's: asked of it, the arrow and the rows come back with its answer.
			headerClick: sortable ? () => this.send(EventKind.Sort, { Column: id }) : undefined,
			// By its header alone, as the thin client's: an edge in every cell takes the click meant for the row.
			resizable: child.Resizable !== false ? 'header' : false,
			formatter: (cell, drawn) => formatCell(this, cell, drawn),
			formatterParams: { parts: [part], grouping: sideBySide },
			editor: cellEditor(this),
			editable: (cell) => mayEdit(cell, this.pressedColumn),
		};
		// The footer, when the table shows one: the column's text and picture — the picture alone when its representation
		// says so, as the header's — where its FooterAlign puts them.
		if (table.Footer === true) {
			definition.bottomCalc = () => '';
			definition.bottomCalcFormatter = (cell, footer) => footerOf(this, footer);
			definition.bottomCalcFormatterParams = {
				text: state.Representation === pictureOnly ? '' : state.FooterText ?? '',
				picture: state.FooterPicture ?? '',
				align: alignOf(child.FooterAlign ?? 0),
			};
		}
		return [definition];
	}

	/**
	 * A group drawn as one column of the grid: its columns' parts one under the other, or side by side in one cell, the
	 * header theirs as their cells are, and the footer.
	 * @param {ProtocolNode} group
	 * @param {any[]} columns its columns' definitions
	 * @param {number} grouping
	 * @param {ProtocolNode} table
	 */
	joined(group, columns, grouping, table) {
		/** @type {Part[]} */
		const parts = columns.flatMap((column) => column.formatterParams?.parts ?? []);
		const footers = table.Footer === true ? columns.map((column) => column.bottomCalcFormatterParams) : undefined;
		return {
			...(footers !== undefined ? {
				bottomCalc: () => '',
				bottomCalcFormatter: (cell, drawn) => {
					const holder = document.createElement('div');
					holder.className = grouping === oneUnderAnother ? 'joined one-under-another' : 'joined in-cell';
					holder.append(...drawn.footers.map((footer) => footerOf(this, footer)));
					return holder;
				},
				bottomCalcFormatterParams: { footers },
			} : {}),
			title: parts.map((part) => part.title).join(grouping === oneUnderAnother ? '\n' : ' '),
			field: `g${group.ControlId}`,
			minWidth: Math.max(...columns.map((column) => column.minWidth ?? 40)),
			headerSort: false,
			headerHozAlign: alignOf(group.HeaderAlign ?? 0),
			titleFormatter: (cell) => {
				const header = document.createElement('span');
				header.className = 'column-header joined';
				header.textContent = cell.getValue();
				return header;
			},
			resizable: group.Resizable !== false ? 'header' : false,
			formatter: (cell, drawn) => formatCell(this, cell, drawn),
			formatterParams: { parts, grouping },
			editor: cellEditor(this),
			editable: (cell) => mayEdit(cell, this.pressedColumn),
		};
	}

	/**
	 * The rows of the table's first Version: the page the State carries, or the first one fetched — and the row the
	 * server stands on, read around when the page does not hold it (a choice form opened on its value).
	 * A reset with no anchor is the server's to place (frmserver tableBox.cpp, Fetch): from the top, or around the row it
	 * stands on with rows ahead of it — the page says whether it is the top (Begin). Taken for the top whatever it said,
	 * a table standing on its last row came back as that one row with no way to the rows above it (a resumed session on
	 * the gallery's grown table, 2026-10-28).
	 * @param {ProtocolNode | undefined} rows
	 * @param {number} current
	 */
	async load(rows, current) {
		try {
			const generation = ++this.generation;
			const page = rows ?? await this.fetch({ Direction: reset, Count: this.perPage() + slack, Before: 0 });
			if (page === null || generation !== this.generation || this.table === null)
				return;
			const read = readPage(page);
			await this.setRows(read.records);
			this.take(read);
			const at = read.current || current;
			if (at !== 0 && !this.select(at))
				this.around(at);
			this.updatePager();
			this.fill();
		}
		finally {
			this.drawn();
		}
	}

	/**
	 * The list read again, the window at its top — the thumb let go at the very top. From the first row when the server
	 * reads from the top; else around the row it stands on, the rows ahead of it with it (see `load`), and the page is
	 * not taken for the top when it does not say so: the rows above stay to be read.
	 */
	async fromTop() {
		if (this.table === null)
			return;
		const generation = ++this.generation;
		const page = await this.fetch({ Direction: reset, Count: this.perPage() + slack, Before: 0 });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		const read = readPage(page);
		await this.setRows(read.records, true);
		this.take(read);
		this.scrollTo(0);
		this.restore(read);
		this.updatePager();
	}

	/**
	 * The rows read anew in place: around the row at the top of the window, which is put back where it stood — from
	 * the State's page when it holds that row (the server reads around its current row, which the person was at),
	 * else fetched with as many rows ahead of it.
	 * @param {ProtocolNode | undefined} rows
	 */
	async reload(rows) {
		if (this.table === null)
			return;
		const generation = ++this.generation;
		// …around a row of the level read: in a tree a row opened is another's, and the crumbs stand over the level.
		const visible = this.table.getRows('visible').filter((row) => row.getData().crumb !== true && !row.getData().level);
		const top = visible[0]?.getData().handle ?? 0;
		const place = this.placeOf(top);
		const wasOpen = new Set(this.table.getRows().filter((row) => row.getData().open).map((row) => row.getData().path));
		// A page of the top is no answer in a folder: there the level is read, whatever the State holds.
		const held = rows !== undefined && this.crumbs.length === 0
			&& (top === 0 || (rows.NodeChildren ?? []).some((row) => row.NodeId === top));
		const page = held ? rows : await this.fetch(top !== 0
			? { Direction: reset, Anchor: top, Count: this.perPage() + slack, Before: slack }
			: { Direction: reset, Count: this.perPage() + slack, Before: 0 });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		const read = readPage(page);
		await this.setRows(read.records, true);
		this.take(read);
		if (wasOpen.size > 0)
			await this.reopen(read.records, wasOpen);
		await this.putBack(top, place);
		if (read.current !== 0)
			this.select(read.current);
		else if (this.currentRow !== 0)
			this.select(this.currentRow, false);
		this.updatePager();
		this.fill();
	}

	/**
	 * The rows around one the page did not hold, it among them and selected.
	 * @param {number} handle
	 */
	async around(handle) {
		const generation = ++this.generation;
		const page = await this.fetch({ Direction: reset, Anchor: handle, Count: this.perPage() + slack, Before: slack });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		const read = readPage(page);
		await this.setRows(read.records);
		this.take(read);
		this.select(read.current || handle);
		this.updatePager();
	}

	/**
	 * What a page read says of the rows now held: the top when it says Begin, the bottom when it says End — the server's
	 * to say, which knows how many it read ahead of the page.
	 * @param {ReturnType<typeof readPage>} read
	 */
	take(read) {
		this.begin = read.begin;
		this.end = read.end;
		this.firstHandle = read.records[0]?.handle ?? 0;
		this.lastHandle = read.records.at(-1)?.handle ?? 0;
	}

	/** The page after the last row held, when there is one — and the rows above the window let go of. */
	async more() {
		if (this.end || this.fetchingAhead || this.lastHandle === 0 || this.table === null)
			return;
		const generation = this.generation;
		const count = this.perPage();
		this.fetchingAhead = true;
		try {
			const page = await this.fetch({ Direction: forward, Anchor: this.lastHandle, Count: count });
			if (page === null || generation !== this.generation || this.table === null)
				return;
			const read = readPage(page);
			await this.table.addData(read.records);
			this.end = read.end || read.records.length < count;
			this.lastHandle = read.records.at(-1)?.handle ?? this.lastHandle;
			this.restore(read);
			await this.trimFront();
		}
		finally {
			this.fetchingAhead = false;
		}
		// Scrolled on past the page while it was read: the next one.
		this.edge(1);
		this.updatePager();
	}

	/** The page before the first row held, when the rows held do not reach the top — the window stays on its rows, and
	 * the rows under it are let go of. */
	async back() {
		if (this.begin || this.fetchingBack || this.firstHandle === 0 || this.table === null)
			return;
		const generation = this.generation;
		const count = this.perPage();
		this.fetchingBack = true;
		try {
			const first = this.firstHandle;
			const page = await this.fetch({ Direction: backward, Anchor: first, Count: count });
			if (page === null || generation !== this.generation || this.table === null)
				return;
			const read = readPage(page);
			const holder = this.holder();
			const stood = holder?.scrollTop ?? 0;
			// A copy: the grid turns the rows it adds at the top round in place, and the page's first would be its last.
			await this.table.addData([...read.records], true);
			this.begin = read.records.length < count;
			this.firstHandle = read.records[0]?.handle ?? first;
			this.restore(read);
			this.scrollTo(stood + read.records.length * this.rowHeight());
			await this.trimTail();
		}
		finally {
			this.fetchingBack = false;
		}
		this.edge(-1);
		this.updatePager();
	}

	/**
	 * The rows held kept to the window and `slack` on each side, the ones above let go of — never one on view or within
	 * `slack` of it; the window stays on its rows (the thin client's trim of the front).
	 *
	 * 🛑 THE CURRENT ROW IS LET GO OF TOO, unlike the thin client's, which pins it: the cursor stands on the first row of
	 * a list just opened, so a pinned one kept every row read past it — the whole list, scrolled to its end. The server
	 * keeps the current row; a page that brings it back selects it again (restore).
	 */
	async trimFront() {
		const shown = this.shown();
		if (shown === null || this.viewMode === treeView)
			return;
		const { rows, top, stood } = shown;
		const cut = Math.min(rows.length - (this.perPage() + 2 * slack), top - slack);
		if (cut <= 0)
			return;
		await this.table?.deleteRow(rows.slice(0, cut));
		this.begin = false;
		this.firstHandle = rows[cut].getData().handle;
		this.scrollTo(stood - cut * this.rowHeight());
	}

	/**
	 * The current row selected again when a page brings it back — let go of while the window was away from it. Read
	 * again, a row is a new object with a new handle; the server names the current one by it (CurrentRow).
	 * @param {ReturnType<typeof readPage>} read
	 */
	restore(read) {
		const current = read.current || this.currentRow;
		if (current !== 0 && read.records.some((record) => record.handle === current))
			this.select(current, false);
	}

	/** …and the rows under it, after a page read above (the thin client's trim of the tail). */
	async trimTail() {
		const shown = this.shown();
		if (shown === null || this.viewMode === treeView)
			return;
		const { rows, bottom } = shown;
		const cut = Math.min(rows.length - (this.perPage() + 2 * slack), rows.length - 1 - bottom - slack);
		if (cut <= 0)
			return;
		await this.table?.deleteRow(rows.slice(rows.length - cut));
		this.end = false;
		this.lastHandle = rows[rows.length - cut - 1].getData().handle;
	}

	/**
	 * The rows held, and the first and the last of them the window shows — by where it stands and the rows' height (the
	 * thin client's line height), not by what the grid drew last: it draws a scroll a frame later, and a trim that read
	 * its drawing let go of rows coming into view. Null before the grid is built.
	 */
	shown() {
		const holder = this.holder();
		if (this.table === null || holder === null)
			return null;
		const rows = this.heldRows();
		const height = this.rowHeight();
		const stood = holder.scrollTop;
		const last = rows.length - 1;
		return {
			rows,
			stood,
			top: Math.min(last, Math.floor(stood / height)),
			bottom: Math.min(last, Math.ceil((stood + holder.clientHeight) / height) - 1),
		};
	}

	/**
	 * The window put where it stood over its rows after rows went or came above it — set, not scrolled by the person:
	 * no way taken, nothing read for it.
	 * @param {number} top
	 */
	scrollTo(top) {
		const holder = this.holder();
		if (holder === null)
			return;
		holder.scrollTop = Math.max(0, top);
		this.scrolled = holder.scrollTop;
	}

	/** The rows held, in their order, without the crumbs frozen over them. */
	heldRows() {
		return (this.table?.getRows() ?? []).filter((row) => row.getData().crumb !== true);
	}

	/**
	 * A page read, with the pictures its rows name — drawn once and whole. Of the level the table stands in unless it
	 * says which (Parent).
	 * @param {object} request
	 */
	async fetch(request) {
		try {
			const page = await this.context.fetch(this.node.ControlId, { Parent: this.crumbs.at(-1)?.handle ?? 0, ...request });
			await this.context.fetchPictures(page);
			return page;
		}
		catch {
			return null;
		}
	}

	/**
	 * A row held, by its handle; null when it is not — asked without the grid's warning for one it does not hold, which
	 * a handle of the rows read before is.
	 * @param {number} handle
	 */
	rowOf(handle) {
		if (handle === 0 || this.table === null)
			return null;
		return this.table.searchRows('handle', '=', handle)[0] ?? null;
	}

	/** The grid's scrolling window over its rows. */
	holder() {
		return /** @type {HTMLElement | null} */ (this.host.querySelector('.tabulator-tableholder'));
	}

	/**
	 * Where a row stands in the window: its top from the window's top; null when it is not drawn.
	 * @param {number} handle
	 */
	placeOf(handle) {
		const element = this.rowOf(handle)?.getElement();
		const holder = this.holder();
		if (!element?.isConnected || holder === null)
			return null;
		return element.getBoundingClientRect().top - holder.getBoundingClientRect().top;
	}

	/**
	 * A row put back where it stood in the window.
	 * @param {number} handle
	 * @param {number | null} place
	 */
	async putBack(handle, place) {
		if (place === null || this.table === null || this.rowOf(handle) === null)
			return;
		await this.table.scrollToRow(handle, 'top', false).catch(() => {});
		const holder = this.holder();
		if (holder === null)
			return;
		holder.scrollTop -= place;
		// Put back, not scrolled by the person: no way taken, nothing read for it.
		this.scrolled = holder.scrollTop;
	}

	/**
	 * The row the server stands on, selected — and brought into view unless said otherwise; false when it is not held.
	 * @param {number} handle
	 * @param {boolean} [show]
	 */
	select(handle, show = true) {
		this.currentRow = handle;
		const row = this.rowOf(handle);
		this.markCursor();
		if (row === null)
			return false;
		if (show)
			row.scrollTo('nearest', false).catch(() => {});
		return true;
	}

	dispose() {
		this.disposed = true;
		clearTimeout(this.secondClick);
		this.drawn();
		this.resized.disconnect();
		this.table?.destroy();
		this.table = null;
	}
}

/**
 * A column's header: its picture, its caption, and the arrow of the sort the server keeps on it — up ascending; one
 * that sorts shows a pointer.
 * @param {Tablebox} owner
 * @param {string} caption
 * @param {{picture: string, sort: number, sortable: boolean}} header
 */
function headerOf(owner, caption, header) {
	const element = document.createElement('span');
	element.className = 'column-header';
	element.classList.toggle('sortable', header.sortable);
	const url = owner.context.picture(header.picture);
	if (url) {
		const image = document.createElement('img');
		image.src = url;
		image.alt = '';
		element.append(image);
	}
	element.append(caption ?? '');
	if (header.sort !== 0) {
		const arrow = document.createElement('span');
		arrow.className = 'sort-arrow';
		arrow.textContent = header.sort > 0 ? '▲' : '▼';
		element.append(arrow);
	}
	return element;
}

/**
 * A table column as a column of the grid draws it: its id, its caption, whether its editor takes typing, and its
 * editor's buttons and kind as it was saved with them.
 * @typedef {{id: number, title: string, typed: boolean, select: boolean, open: boolean, clear: boolean,
 *   password: boolean, multiline: boolean}} Part
 */

/**
 * The parts a column of the grid draws — one, or a group's.
 * @param {any} cell
 * @returns {Part[]}
 */
function partsOf(cell) {
	return cell.getColumn().getDefinition().formatterParams?.parts ?? [];
}

/** The ids of the columns a column of the grid draws. @param {any} cell */
function columnsOf(cell) {
	return partsOf(cell).map((part) => part.id);
}

/**
 * The part of a cell a column names — the cell's only one when it names none of them.
 * @param {any} cell
 * @param {number} column
 */
function partOf(cell, column) {
	const parts = partsOf(cell);
	return parts.find((part) => part.id === column) ?? (parts.length === 1 ? parts[0] : undefined);
}

/**
 * May this cell's editor open — the server wrote its value. A column that takes no typing still has its buttons (`typed`).
 * @param {any} cell
 * @param {number} column
 */
function mayEdit(cell, column) {
	const part = partOf(cell, column);
	return part !== undefined && cell.getRow().getData()[`e${part.id}`] === true;
}

/**
 * The table column a press landed on — the cell or the part of a group's cell that says it.
 * @param {EventTarget | null} target
 */
function columnUnder(target) {
	const marked = target instanceof Element ? target.closest('[data-column]') : null;
	return marked instanceof HTMLElement ? Number(marked.dataset.column) : 0;
}

/**
 * A cell's editor — its column's text editor (the part pressed, of a group's), the buttons inside it as the column was
 * saved with them: Select (…), Open (▷), Clear (×). Opened, it tells the server the cell is typed into (Input); Enter,
 * Tab or leaving it commits what was typed (Change {Row, Column, Text}), Escape lets it go. The cell keeps the server's
 * text either way: what the server takes comes with the rows read anew, an edit it refuses leaves the cell as it was.
 * A button lets the edit go — nothing typed is committed to race the value it brings — and asks the server for its act
 * on the cell, {Row, Column}.
 * @param {Tablebox} owner
 */
function cellEditor(owner) {
	return (cell, onRendered, success, cancel) => {
		const part = partOf(cell, owner.pressedColumn) ?? partsOf(cell).find((one) => mayEdit(cell, one.id));
		if (part === undefined) {
			cancel();
			return document.createElement('span');
		}
		const row = cell.getRow().getData().handle;
		const text = cell.getRow().getData()[`c${part.id}`] ?? '';
		const editor = new TextEditor({ multiline: part.multiline, password: part.password });
		const field = editor.field;
		field.readonly = !part.typed;
		if (part.multiline)
			field.setAttribute('rows', '1');
		let done = false;
		const finish = (/** @type {boolean} */ commit) => {
			if (done)
				return;
			done = true;
			const typed = field.value ?? text;
			cancel();
			if (commit && typed !== text)
				owner.send(EventKind.Change, { Row: row, Column: part.id, Text: typed });
		};
		owner.awaitEdit = false;
		owner.send(EventKind.Input, { Row: row, Column: part.id });
		field.classList.add('cell-editor');
		field.value = text;
		editor.showButtons({ ButtonSelect: part.select, ButtonOpen: part.open, ButtonClear: part.clear });
		editor.onButton = (kind) => {
			finish(false);
			owner.send(kind, { Row: row, Column: part.id });
		};
		field.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
			if ((event.key === 'Enter' && !event.shiftKey) || event.key === 'Tab')
				finish(true);
			else if (event.key === 'Escape')
				finish(false);
			else
				return;
			// The keys go on among the rows.
			event.preventDefault();
			owner.host.focus({ preventScroll: true });
		});
		field.addEventListener('blur', () => finish(true));
		onRendered(() => editor.focus());
		return field;
	};
}

/**
 * A cell drawn as text — never as markup, whatever the text holds — with its look and a flag's mark; a group's drawn
 * as one, each of its columns' parts one under the other or side by side, each marked with its column. A group's row is
 * its caption alone, in the first column, running on over the others. The first column has the row's own before it.
 * @param {Tablebox} owner
 * @param {any} cell
 * @param {{parts: Part[], grouping: number, first?: boolean}} drawn
 */
function formatCell(owner, cell, drawn) {
	const record = cell.getRow().getData();
	const element = cell.getElement();
	if (drawn.parts.length === 1)
		element.dataset.column = String(drawn.parts[0].id);
	/** @type {Node} */
	let content;
	if (record.group)
		content = document.createTextNode(drawn.first === true ? record.group : '');
	else if (drawn.parts.length === 1)
		content = drawPart(element, record, drawn.parts[0].id);
	else {
		const holder = document.createElement('div');
		holder.className = drawn.grouping === oneUnderAnother ? 'joined one-under-another' : 'joined in-cell';
		for (const part of drawn.parts) {
			const line = document.createElement('div');
			line.dataset.column = String(part.id);
			line.append(drawPart(line, record, part.id));
			holder.append(line);
		}
		content = holder;
	}
	return drawn.first === true ? firstCell(owner, cell, record, content) : content;
}

/**
 * The first column's cell, as the data view draws it in every view: stepped in by its level, the place of the expander —
 * a folder's in a tree a button, in the Hierarchical view a mark, a crumb's open — the row's picture, what the cell shows.
 * @param {Tablebox} owner
 * @param {any} cell
 * @param {any} record
 * @param {Node} content
 */
function firstCell(owner, cell, record, content) {
	const holder = document.createElement('span');
	holder.className = 'first-cell';
	holder.style.paddingInlineStart = `${(record.level ?? 0) * treeIndent}px`;
	const folder = record.container === true || record.crumb === true;
	const open = record.open === true || record.crumb === true;
	const button = folder && owner.viewMode === treeView;
	const expander = document.createElement(button ? 'button' : 'span');
	expander.className = 'expander';
	if (folder)
		expander.textContent = open ? '▾' : '▸';
	else
		expander.style.visibility = 'hidden';
	if (button) {
		expander.setAttribute('type', 'button');
		expander.setAttribute('aria-expanded', String(open));
		expander.setAttribute('aria-label', open ? 'Close' : 'Open');
		// Its own: neither a move of the cursor nor an activation of the row.
		expander.addEventListener('click', (event) => {
			event.stopPropagation();
			owner.toggle(cell.getRow());
		});
		expander.addEventListener('dblclick', (event) => event.stopPropagation());
	}
	holder.append(expander);
	const url = owner.context.picture(record.picture);
	if (url) {
		const image = document.createElement('img');
		image.src = url;
		image.alt = '';
		holder.append(image);
	}
	const text = document.createElement('span');
	text.className = 'first-text';
	text.append(content);
	holder.append(text);
	return holder;
}

/**
 * One column's text of a row, its look laid on the element that shows it.
 * @param {HTMLElement} element
 * @param {any} record
 * @param {number} id the column's
 */
function drawPart(element, record, id) {
	const look = record[`s${id}`] ?? {};
	element.style.textAlign = look.textAlign ?? (record[`n${id}`] ? 'right' : '');
	element.style.color = look.color ?? '';
	element.style.backgroundColor = look.backgroundColor ?? '';
	element.style.fontWeight = look.fontWeight ?? '';
	element.style.fontStyle = look.fontStyle ?? '';
	element.style.textDecoration = look.textDecoration ?? '';
	element.style.fontFamily = look.fontFamily ?? '';
	element.style.fontSize = look.fontSize ?? '';
	const checked = record[`k${id}`];
	// A flag is a tick, not a box: a list's cell is not ticked by a click (the thin client's).
	return document.createTextNode(checked === undefined ? record[`c${id}`] ?? '' : checked ? '✓' : '');
}

/**
 * A column's footer: its picture and its text, standing where its alignment says.
 * @param {Tablebox} owner
 * @param {{text: string, picture: string, align: string}} footer
 */
function footerOf(owner, footer) {
	const element = document.createElement('span');
	element.className = 'column-footer';
	element.style.justifyContent = footer.align === 'right' ? 'flex-end' : footer.align === 'center' ? 'center' : '';
	const url = owner.context.picture(footer.picture);
	if (url) {
		const image = document.createElement('img');
		image.src = url;
		image.alt = '';
		element.append(image);
	}
	element.append(footer.text);
	return element;
}

/** A header's alignment for the grid, from wx's flags. @param {number} flags */
function alignOf(flags) {
	return (flags & right) !== 0 ? 'right' : (flags & centred) !== 0 ? 'center' : 'left';
}

/**
 * The first column of the grid that is shown — one of a group's standing under its title too.
 * @param {any[]} definitions
 * @returns {any}
 */
function firstShown(definitions) {
	for (const definition of definitions) {
		const found = definition.columns !== undefined ? firstShown(definition.columns)
			: definition.visible !== false ? definition : undefined;
		if (found !== undefined)
			return found;
	}
	return undefined;
}

/**
 * What a row of a tree is known by when it is read again with another handle: what it reads.
 * @param {any} record
 */
function keyOf(record) {
	return Object.keys(record).filter((name) => /^c\d+$/.test(name)).map((name) => record[name]).join('\t');
}

/** A grid field's column id: `c<id>` → id; 0 for a group drawn as one. @param {string} field */
function idOf(field) {
	return field?.startsWith('c') ? Number(field.slice(1)) : 0;
}

defineControl('Tablebox', Tablebox);
