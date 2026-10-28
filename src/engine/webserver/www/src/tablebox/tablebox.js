// A TABLE (Tablebox) — its command bar, its columns (the node's children: TableboxColumn, and a column group's own),
// and its rows, read by pages from the server and never all at once: a list is paged in the engine, and pretending
// otherwise on the way to the browser would mean reading a whole catalogue to draw twenty lines of it. The grid is
// Tabulator (vendor/tabulator), loaded only when a form has a table on it, and on a narrow screen it folds the columns
// that do not fit into the row they belong to. The thin client's table (frmclient/visualView/ctrl/tableBox.cpp) is
// the one this follows.
//
// The first page comes in the State (Rows, protocol 5) or is fetched from the top; the next ones are fetched as the
// person scrolls — after the last row held (Direction 1) and before the first (Direction -1), keyset, never an offset.
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

import { Control, EventKind, defineControl } from '../controls/control.js';
import { CommandBar } from '../controls/commandBar.js';
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
		/** a row activated with no cell the client may edit: the editor the server opens is opened here */
		this.awaitEdit = false;
		/** @type {ProtocolNode | undefined} the table's standard commands, as its State offers them */
		this.contextMenu = undefined;
		this.host.addEventListener('pointerdown', (event) => {
			this.pressedColumn = columnUnder(event.target);
		}, true);
		this.listenPager();
		// A window grown taller shows more rows than are held past it: read on, and the scrollbar placed again.
		this.resized = new ResizeObserver(() => {
			this.updatePager();
			this.fill();
		});
		this.resized.observe(this.frame);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.bar.update(this.state(node).CommandBar);
		this.contextMenu = this.state(node).ContextMenu;
		this.host.classList.toggle('no-header', node.Header === false);
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
		const definitions = [pictureColumn(this), ...(node.NodeChildren ?? []).flatMap((child) => this.definitionsOf(child, node))];
		// The columns frozen at the left (its FrezeeCol), the rows' pictures with them.
		const frozen = node.FrezeeCol ?? 0;
		if (frozen > 0)
			definitions.slice(0, frozen + 1).forEach((definition) => { definition.frozen = true; });
		const drawn = JSON.stringify(definitions, (name, value) => typeof value === 'function' ? undefined : value);
		const state = this.state(node);
		this.grid(definitions, node).then((table) => {
			if (this.disposed)
				return;
			if (drawn !== this.drawnColumns) {
				this.drawnColumns = drawn;
				table.setColumns(definitions);
			}
			if (state.Version !== this.version) {
				const first = this.version === undefined;
				this.version = state.Version;
				if (first)
					this.load(state.Rows, state.CurrentRow ?? 0);
				else
					this.reload(state.Rows);
			}
			else if (state.CurrentRow)
				this.select(state.CurrentRow);
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
				selectableRows: 1,
				placeholder: '',
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
		table.on('cellClick', (event, cell) => this.sendCursor(cell));
		table.on('cellDblClick', (event, cell) => {
			// The desktop's ActivateRow: the cell clicked opens its editor when it may be edited (the grid opens it), else
			// the row's current or first that may — and with none, the server opens one if it will.
			if (!mayEdit(cell, this.pressedColumn) && !this.editCurrentRow())
				this.awaitEdit = true;
			// What else activating it is the server decides — the row's object opened, a choice made.
			this.send(EventKind.Open, { Row: cell.getRow().getData().handle });
		});
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
	 * A page read the way the person goes when fewer than `slack` rows are left past the window's edge — only that way:
	 * read the other way too, a window short of rows behind it jumped as its far side was let go (the thin client's
	 * OnScrollEvent).
	 * @param {number} direction 1 down, -1 up
	 */
	edge(direction) {
		const holder = this.holder();
		if (holder === null)
			return;
		const margin = slack * this.rowHeight();
		if (direction > 0 && holder.scrollHeight - holder.scrollTop - holder.clientHeight < margin)
			this.more();
		else if (direction < 0 && holder.scrollTop < margin)
			this.back();
	}

	/** Rows read on while those held do not reach `slack` past the window — a short first page, a window grown. */
	fill() {
		const holder = this.holder();
		if (holder !== null && holder.scrollHeight - holder.clientHeight < slack * this.rowHeight())
			this.more();
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
		const handle = cell.getRow().getData().handle;
		const column = this.pressedColumn || idOf(cell.getField());
		if (handle === this.currentRow && column === this.currentColumn)
			return;
		if (handle !== this.currentRow)
			this.awaitEdit = false;
		this.currentRow = handle;
		this.currentColumn = column;
		this.send(EventKind.Row, column !== 0 ? { Row: handle, Column: column } : { Row: handle });
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
			const columns = (child.NodeChildren ?? []).flatMap((one) => this.definitionsOf(one, table));
			const laid = grouping === sideBySide || columns.length < 2 ? columns : [this.joined(child, columns, grouping)];
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
			editable: state.ReadOnly !== true && child.TexteditMode !== false,
			select: child.ButtonSelect !== false,
			open: child.ButtonOpen === true,
			clear: child.ButtonClear !== false,
			password: child.PasswordMode === true,
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
			resizable: child.Resizable !== false,
			formatter: formatCell,
			formatterParams: { parts: [part], grouping: sideBySide },
			editor: part.editable ? cellEditor(this) : false,
			editable: (cell) => mayEdit(cell, this.pressedColumn),
		};
		// The footer, when the table shows one: the column's text and picture, where its FooterAlign puts them.
		if (table.Footer === true) {
			definition.bottomCalc = () => '';
			definition.bottomCalcFormatter = (cell, footer) => footerOf(this, footer);
			definition.bottomCalcFormatterParams = { text: state.FooterText ?? '', picture: state.FooterPicture ?? '',
				align: alignOf(child.FooterAlign ?? 0) };
		}
		return [definition];
	}

	/**
	 * A group drawn as one column of the grid: its columns' parts one under the other, or side by side in one cell, the
	 * header theirs as their cells are.
	 * @param {ProtocolNode} group
	 * @param {any[]} columns its columns' definitions
	 * @param {number} grouping
	 */
	joined(group, columns, grouping) {
		/** @type {Part[]} */
		const parts = columns.flatMap((column) => column.formatterParams?.parts ?? []);
		return {
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
			resizable: group.Resizable !== false,
			formatter: formatCell,
			formatterParams: { parts, grouping },
			editor: parts.some((part) => part.editable) ? cellEditor(this) : false,
			editable: (cell) => mayEdit(cell, this.pressedColumn),
		};
	}

	/**
	 * The rows of the table's first Version: the page the State carries, or the first one fetched from the top — and
	 * the row the server stands on, read around when the page does not hold it (a choice form opened on its value).
	 * @param {ProtocolNode | undefined} rows
	 * @param {number} current
	 */
	async load(rows, current) {
		const generation = ++this.generation;
		const page = rows ?? await this.fetch({ Direction: reset, Count: this.perPage() + slack, Before: 0 });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		const read = readPage(page);
		await this.table.setData(read.records);
		this.take(read, read.anchor !== 0 ? read.count : 0);
		const at = read.current || current;
		if (at !== 0 && !this.select(at))
			this.around(at);
		this.updatePager();
		this.fill();
	}

	/** The list read again from its first row, the window at its top — the thumb let go at the very top. */
	async fromTop() {
		if (this.table === null)
			return;
		const generation = ++this.generation;
		const page = await this.fetch({ Direction: reset, Count: this.perPage() + slack, Before: 0 });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		const read = readPage(page);
		await this.table.replaceData(read.records);
		this.take(read, 0);
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
		const visible = this.table.getRows('visible');
		const top = visible[0]?.getData().handle ?? 0;
		const place = this.placeOf(top);
		const held = rows !== undefined && (top === 0 || (rows.NodeChildren ?? []).some((row) => row.NodeId === top));
		const ahead = held ? (rows?.Anchor ? rows.Count ?? 0 : 0) : top !== 0 ? slack : 0;
		const page = held ? rows : await this.fetch(top !== 0
			? { Direction: reset, Anchor: top, Count: this.perPage() + slack, Before: slack }
			: { Direction: reset, Count: this.perPage() + slack, Before: 0 });
		if (page === null || generation !== this.generation || this.table === null)
			return;
		const read = readPage(page);
		await this.table.replaceData(read.records);
		this.take(read, ahead);
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
		await this.table.setData(read.records);
		this.take(read, slack);
		this.select(read.current || handle);
		this.updatePager();
	}

	/**
	 * What a page read says of the rows now held: the top reached when fewer rows came ahead than were asked (none asked
	 * — a read from the top), the bottom when it says End.
	 * @param {ReturnType<typeof readPage>} read
	 * @param {number} ahead the rows asked ahead of its anchor
	 */
	take(read, ahead) {
		this.begin = ahead === 0 || read.before < ahead;
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
		if (shown === null)
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
		if (shown === null)
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
		const rows = this.table.getRows();
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

	/**
	 * A page read, with the pictures its rows name — drawn once and whole.
	 * @param {object} request
	 */
	async fetch(request) {
		try {
			const page = await this.context.fetch(this.node.ControlId, request);
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
		if (row === null)
			return false;
		this.table?.deselectRow();
		row.select();
		if (show)
			row.scrollTo('nearest', false).catch(() => {});
		return true;
	}

	dispose() {
		this.disposed = true;
		this.resized.disconnect();
		this.table?.destroy();
		this.table = null;
	}
}

/**
 * The column of the rows' own pictures — a folder, an element, a document posted — narrow, before the others.
 * @param {Tablebox} owner
 */
function pictureColumn(owner) {
	return {
		field: 'picture',
		title: '',
		width: 28,
		headerSort: false,
		resizable: false,
		formatter: (cell) => {
			const url = owner.context.picture(cell.getValue());
			if (!url)
				return '';
			const image = document.createElement('img');
			image.src = url;
			image.alt = '';
			return image;
		},
	};
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
 * A table column as a column of the grid draws it: its id, its caption, whether it takes edits, and its editor's
 * buttons as it was saved with them.
 * @typedef {{id: number, title: string, editable: boolean, select: boolean, open: boolean, clear: boolean,
 *   password: boolean}} Part
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
 * May the person type into this cell's column — the server wrote its value, and the column takes edits.
 * @param {any} cell
 * @param {number} column
 */
function mayEdit(cell, column) {
	const part = partOf(cell, column);
	return part !== undefined && part.editable && cell.getRow().getData()[`e${part.id}`] === true;
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
 * saved with them: Select (…), Open (▷), Clear (×). Opened, it tells the server the cell is typed into (Input); Enter
 * or leaving it commits what was typed (Change {Row, Column, Text}), Escape lets it go. The cell keeps the server's
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
		let done = false;
		const finish = (/** @type {boolean} */ commit) => {
			if (done)
				return;
			done = true;
			cancel();
			if (commit && input.value !== text)
				owner.send(EventKind.Change, { Row: row, Column: part.id, Text: input.value });
		};
		owner.awaitEdit = false;
		owner.send(EventKind.Input, { Row: row, Column: part.id });
		const editor = document.createElement('div');
		editor.className = 'cell-editor';
		const input = document.createElement('input');
		input.type = part.password ? 'password' : 'text';
		input.value = text;
		editor.append(input);
		for (const [shown, glyph, title, kind] of /** @type {[boolean, string, string, number][]} */ ([
			[part.select, '…', 'Select', EventKind.Select],
			[part.open, '▷', 'Open', EventKind.Open],
			[part.clear, '×', 'Clear', EventKind.Clear],
		])) {
			if (!shown)
				continue;
			const button = document.createElement('button');
			button.type = 'button';
			button.textContent = glyph;
			button.title = title;
			button.setAttribute('aria-label', title);
			button.addEventListener('mousedown', (event) => event.preventDefault());
			button.addEventListener('click', () => {
				finish(false);
				owner.send(kind, { Row: row, Column: part.id });
			});
			editor.append(button);
		}
		input.addEventListener('keydown', (event) => {
			if (event.key === 'Enter')
				finish(true);
			else if (event.key === 'Escape')
				finish(false);
		});
		input.addEventListener('blur', () => finish(true));
		onRendered(() => {
			input.focus();
			input.select();
		});
		return editor;
	};
}

/**
 * A cell drawn as text — never as markup, whatever the text holds — with its look and a flag's mark; a group's drawn
 * as one, each of its columns' parts one under the other or side by side, each marked with its column.
 * @param {any} cell
 * @param {{parts: Part[], grouping: number}} drawn
 */
function formatCell(cell, drawn) {
	const record = cell.getRow().getData();
	const element = cell.getElement();
	if (drawn.parts.length === 1) {
		element.dataset.column = String(drawn.parts[0].id);
		return drawPart(element, record, drawn.parts[0].id);
	}
	const holder = document.createElement('div');
	holder.className = drawn.grouping === oneUnderAnother ? 'joined one-under-another' : 'joined in-cell';
	for (const part of drawn.parts) {
		const line = document.createElement('div');
		line.dataset.column = String(part.id);
		line.append(drawPart(line, record, part.id));
		holder.append(line);
	}
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
	return document.createTextNode(checked === undefined ? record[`c${id}`] ?? '' : checked ? '☑' : '☐');
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

/** A grid field's column id: `c<id>` → id; 0 for the picture column. @param {string} field */
function idOf(field) {
	return field?.startsWith('c') ? Number(field.slice(1)) : 0;
}

defineControl('Tablebox', Tablebox);
