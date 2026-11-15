// THE REGISTRATION JOURNAL — a tab the server opened (Kind AuditLog), as the thin client's (ibAuditLogDocument and its
// view): what the installation did — logins, writes, postings — read from the server page by page, newest first, by
// the filter over it: From and To (days, To inclusive), the Level from which («Audit only» at first), the Source, the
// User, a part of the Message. Apply and Refresh read it again from the top, Clear puts the filter back as it was,
// Live refresh reads it again every three seconds. A row's object — the row double-clicked, Enter, Open object — is
// opened by the server, where the configuration is: event Open {Ref, RefMeta} on the tab.
//
// Fetch {Tab, Request: {From, To, Level, User, Source, Search, Offset, Count}} → the rows, each {Time (ms), Level,
// Session, User, Host, Source, Event, Message, Ref, RefMeta}.

import { EventKind } from '../controls/control.js';
import { CommandBar } from '../controls/commandBar.js';
import { _ } from '../localization.js';
import { TabDocument, defineDocument } from './tabDocument.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../window/window.js').ClientWindow} ClientWindow */

// The journal's kind of tab (ibView::ibKind).
const auditLog = 4;

// The rows read at once (ibAuditLogModel::kPageSize).
const pageSize = 200;

// The levels a person filters by, each with the least level it reads; «Audit only» at first. Not translated — the
// thin client's are not either.
/** @type {[string, number][]} */
const levels = [['All', -1], ['Info+', 0], ['Warn+', 1], ['Error+', 2], ['Audit only', 3]];
const auditOnly = 4;
const levelTags = ['INFO', 'WARN', 'ERROR', 'AUDIT'];
// The sources producers write — none: any.
const sources = ['', 'auth', 'session', 'record', 'document', 'metadata'];

// The tools, with the backend pictures the thin client's bar shows (g_picFilterSet, g_picFilterClear, g_picUpdateForm,
// g_picSelect — picture ids, past what a number keeps: as the wire carries them). Their words and the columns' are
// said when drawn, in the person's language.
const Tool = Object.freeze({ Apply: 1, Clear: 2, Refresh: 3, Open: 4 });
const tools = () => [
	{ Id: Tool.Apply, Caption: _('Apply'), Picture: '633900438294025713' },
	{ Id: Tool.Clear, Caption: _('Clear'), Picture: '633918030480077089' },
	{ Id: Tool.Refresh, Caption: _('Refresh'), Picture: '645726728081437019' },
	{ Separator: true },
	{ Id: Tool.Open, Caption: _('Open object'), Picture: '603586643630906693' },
];

// The columns, their widths the thin client's.
/** @returns {[string, number][]} */
const columns = () => [[_('Time'), 140], [_('Level'), 60], [_('User'), 120], [_('Source'), 80], [_('Event'), 100],
	[_('Message'), 320], [_('Ref'), 80]];

export class AuditLogDocument extends TabDocument {
	/**
	 * @param {ClientWindow} window
	 * @param {number} tab
	 */
	constructor(window, tab) {
		super(window, tab);
		this.from = dateInput();
		this.to = dateInput();
		this.level = choice(levels.map(([caption]) => caption));
		this.source = choice(sources.map((source) => source || _('any')));
		this.user = textInput('8rem');
		this.search = textInput('');
		this.live = document.createElement('input');
		this.live.type = 'checkbox';
		/** @type {any} what the bar asks of its owner: the event of an entry pressed, a picture's URL */
		const owner = {
			send: (/** @type {number} */ _kind, /** @type {{Id: number}} */ args) => this.command(args.Id),
			context: { picture: (/** @type {string} */ id) => this.window.pictures.url(id) },
		};
		this.bar = new CommandBar(owner);
		this.table = document.createElement('table');
		this.holder = document.createElement('div');
		/** @type {ProtocolNode[]} the rows read, in their order */
		this.rows = [];
		/** the row taken — none: -1 */
		this.taken = -1;
		/** the filter the rows were read by */
		this.filter = {};
		/** a page is being read */
		this.reading = false;
		/** the last page read was short: there is no more */
		this.ended = false;
		/** each reading from the top — a page of an older one is let go */
		this.generation = 0;
		/** @type {ReturnType<typeof setInterval> | 0} */
		this.timer = 0;
	}

	/** @param {ProtocolNode} _view */
	drawView(_view) {
		if (this.element !== null)
			return;
		this.element = document.createElement('div');
		this.element.className = 'audit-log';

		const strip = document.createElement('div');
		strip.className = 'audit-log-filter';
		/** @type {[string, HTMLElement][]} */
		const filters = [[_('From:'), this.from], [_('To:'), this.to], [_('Level:'), this.level],
			[_('Source:'), this.source], [_('User:'), this.user], [_('Search:'), this.search]];
		for (const [caption, input] of filters) {
			const label = document.createElement('label');
			label.append(caption, input);
			strip.append(label);
		}
		for (const input of [this.user, this.search]) {
			input.addEventListener('keydown', (event) => {
				if (event.key === 'Enter')
					this.reload();
			});
		}

		const toolbar = document.createElement('div');
		toolbar.className = 'audit-log-tools';
		const live = document.createElement('label');
		live.append(this.live, _('Live refresh (3 s)'));
		this.live.addEventListener('change', () => {
			clearInterval(this.timer);
			this.timer = this.live.checked ? setInterval(() => this.reload(), 3000) : 0;
		});
		toolbar.append(this.bar.element, live);

		this.table.className = 'audit-log-table';
		const head = this.table.createTHead().insertRow();
		for (const [caption, width] of columns()) {
			const cell = document.createElement('th');
			cell.textContent = caption;
			cell.style.width = `${width}px`;
			head.append(cell);
		}
		this.table.createTBody();
		this.holder.className = 'audit-log-rows';
		this.holder.tabIndex = 0;
		this.holder.append(this.table);
		this.holder.addEventListener('scroll', () => this.readOn());
		this.holder.addEventListener('keydown', (event) => this.key(event));

		this.element.append(strip, toolbar, this.holder);
		this.window.views.append(this.element);
		const bar = tools();
		this.window.pictures.fetchFor({ NodeChildren: bar.map((tool) => ({ Picture: tool.Picture })) })
			.catch(() => {})
			.then(() => this.bar.update({ NodeChildren: bar }));
		this.clear();
	}

	/**
	 * A tool pressed.
	 * @param {number} tool (Tool)
	 */
	command(tool) {
		if (tool === Tool.Apply || tool === Tool.Refresh)
			this.reload();
		else if (tool === Tool.Clear)
			this.clear();
		else if (tool === Tool.Open)
			this.openObject();
	}

	/** The filter as it was first — the last seven days, «Audit only», nothing else — and the rows read by it. */
	clear() {
		const today = new Date();
		const weekAgo = new Date(today);
		weekAgo.setDate(today.getDate() - 7);
		this.from.value = dayText(weekAgo);
		this.to.value = dayText(today);
		this.level.selectedIndex = auditOnly;
		this.source.selectedIndex = 0;
		this.user.value = '';
		this.search.value = '';
		this.reload();
	}

	/** The filter as the strip says it (ReadFilter): days from midnight, To's day whole. */
	readFilter() {
		/**
		 * @param {HTMLInputElement} input
		 * @param {number} after the days after it
		 */
		const day = (input, after) => {
			if (input.value === '')
				return 0;
			const date = new Date(`${input.value}T00:00:00`);
			date.setDate(date.getDate() + after);
			return date.getTime();
		};
		return { From: day(this.from, 0), To: day(this.to, 1), Level: levels[this.level.selectedIndex]?.[1] ?? -1,
			Source: sources[this.source.selectedIndex] ?? '', User: this.user.value, Search: this.search.value };
	}

	/** Read again from the top, by the filter as the strip says it now. */
	reload() {
		this.generation++;
		this.filter = this.readFilter();
		this.rows = [];
		this.taken = -1;
		this.reading = false;
		this.ended = false;
		this.table.tBodies[0].replaceChildren();
		this.loadPage();
	}

	/** The next page, after the rows read. */
	async loadPage() {
		if (this.reading || this.ended)
			return;
		const generation = this.generation;
		this.reading = true;
		try {
			const answer = await this.window.session.call('fetch',
				{ Tab: this.tab, Request: { ...this.filter, Offset: this.rows.length, Count: pageSize } });
			if (generation !== this.generation)
				return;
			/** @type {ProtocolNode[]} */
			const page = answer?.NodeChildren ?? [];
			this.ended = page.length < pageSize;
			const from = this.rows.length;
			this.rows.push(...page);
			this.table.tBodies[0].append(...page.map((entry, at) => this.rowOf(entry, from + at)));
			this.reading = false;
			this.readOn();
		}
		catch (error) {
			if (generation === this.generation)
				this.reading = false;
			this.window.refused(/** @type {Error} */ (error));
		}
	}

	/** The next page read while the rows come near the end of the window — or do not fill it. */
	readOn() {
		const holder = this.holder;
		const below = holder.scrollHeight - holder.scrollTop - holder.clientHeight;
		if (holder.isConnected && below < holder.clientHeight / 2)
			this.loadPage();
	}

	/**
	 * @param {ProtocolNode} entry
	 * @param {number} index its place among the rows read
	 */
	rowOf(entry, index) {
		const row = document.createElement('tr');
		const ref = String(entry.Ref ?? '');
		const texts = [timeText(new Date(Number(entry.Time ?? 0))), levelTags[Number(entry.Level)] ?? '?', entry.User,
			entry.Source, entry.Event, entry.Message, ref === '' ? '' : `${ref.slice(0, 8)}...`];
		for (const text of texts)
			row.insertCell().textContent = String(text ?? '');
		row.addEventListener('click', () => this.take(index));
		row.addEventListener('dblclick', () => {
			this.take(index);
			this.openObject();
		});
		return row;
	}

	/**
	 * The row taken, shown in its place.
	 * @param {number} index
	 */
	take(index) {
		const rows = this.table.tBodies[0].rows;
		rows[this.taken]?.classList.remove('current');
		this.taken = index;
		rows[index]?.classList.add('current');
		rows[index]?.scrollIntoView({ block: 'nearest' });
	}

	/**
	 * The keys of the rows: up and down, Enter opens the row's object.
	 * @param {KeyboardEvent} event
	 */
	key(event) {
		const step = { ArrowDown: 1, ArrowUp: -1, PageDown: 20, PageUp: -20 }[event.key];
		if (step !== undefined && this.rows.length > 0) {
			event.preventDefault();
			this.take(Math.max(0, Math.min(this.rows.length - 1, this.taken + step)));
		}
		else if (event.key === 'Enter') {
			event.preventDefault();
			this.openObject();
		}
	}

	/** The object of the row taken, opened by the server — a row about none opens nothing. */
	openObject() {
		const entry = this.rows[this.taken];
		if (entry === undefined || !entry.Ref)
			return;
		this.window.session.send({ Event: EventKind.Open, Args: { Ref: entry.Ref, RefMeta: entry.RefMeta ?? 0 },
			Tab: this.tab }).catch((error) => this.window.refused(error));
	}

	close() {
		clearInterval(this.timer);
		this.generation++;
		super.close();
	}
}
defineDocument(auditLog, AuditLogDocument);

/** A day picked (the thin client's date picker, its century shown). */
function dateInput() {
	const input = document.createElement('input');
	input.type = 'date';
	return input;
}

/** @param {string} width — none: what is left */
function textInput(width) {
	const input = document.createElement('input');
	input.type = 'text';
	if (width !== '')
		input.style.width = width;
	return input;
}

/** @param {string[]} captions */
function choice(captions) {
	const select = document.createElement('select');
	for (const caption of captions)
		select.append(new Option(caption));
	return select;
}

/** A day as a date input holds it — the person's own, not UTC's. @param {Date} date */
function dayText(date) {
	const two = (/** @type {number} */ value) => String(value).padStart(2, '0');
	return `${date.getFullYear()}-${two(date.getMonth() + 1)}-${two(date.getDate())}`;
}

/** A row's time as the thin client writes it (%Y-%m-%d %H:%M:%S), the person's own. @param {Date} date */
function timeText(date) {
	const two = (/** @type {number} */ value) => String(value).padStart(2, '0');
	return `${dayText(date)} ${two(date.getHours())}:${two(date.getMinutes())}:${two(date.getSeconds())}`;
}
