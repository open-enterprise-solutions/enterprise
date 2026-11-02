// THE SERVER THE PAGE TALKS TO IN THE TESTS — a socket put in the place of WebSocket before the page starts: it takes the
// page's OES-RPC packets and answers with packets, by the page's own packet.js. It answers as frmserver's table does:
// the protocol's shape (docs/public/client-protocol.md) and a read by level, around a row, both ways (tableBox.cpp, Fetch).
// A scenario is `window.fakeScenario`, set before this script runs. What the page sent stands in `window.fake.calls`.

(() => {
	const scenario = window.fakeScenario ?? {};
	const packet = import('/pub/src/protocol/packet.js');

	// The events (ibEvent::ibKind) and the questions (ibRequest::ibKind) this server knows of.
	const Event = { Command: 2, Open: 7 };
	const messageRequest = 1;
	const viewModeRequest = 7;
	const viewModeCommand = 700;
	// What a tab's view shows when it is no form (ibView::ibKind): the start page.
	const homePageKind = 16;
	// A message's buttons, wx's flags and codes alike.
	const yes = 0x02;
	const no = 0x08;
	const cancel = 0x10;
	// An item's Stretch (wxStretch): it fills its box across.
	const expand = 0x2000;

	class FakeServer {
		constructor() {
			this.calls = [];
			this.frame = 0;
			this.latency = scenario.latency ?? 0;
			this.fetchLatency = scenario.fetchLatency ?? this.latency;
			this.loginLatency = scenario.loginLatency ?? this.latency;
			this.users = scenario.users ?? [];
			this.password = scenario.password ?? '';
			/** @type {any[]} the forms open, a tab each */
			this.tabs = (scenario.tabs ?? []).map((tab, place) => ({ id: place + 1, ...tab }));
			this.active = this.tabs.length > 0 ? 1 : 0;
			this.request = undefined;
			this.nextRequest = 1;
			/** @type {Map<string, any>} the forms drawn, by their Key */
			this.forms = new Map();
		}

		/** The events the page sent of one kind, or every one. @param {number} [kind] */
		events(kind) {
			return this.calls.filter((call) => call.method === 'event' && (kind === undefined || call.params.Event === kind))
				.map((call) => call.params);
		}

		reads() {
			return this.calls.filter((call) => call.method === 'fetch').map((call) => call.params.Request);
		}

		tab() {
			return this.tabs.find((tab) => tab.id === this.active);
		}

		/** A tab opened from outside — as a row activated would open it. @param {any} tab */
		open(tab) {
			const id = Math.max(0, ...this.tabs.map((one) => one.id)) + 1;
			this.tabs.push({ id, ...tab });
			this.active = id;
			this.push();
		}

		/** The server moved: the page is told, and reads the frame. */
		push() {
			for (const socket of FakeSocket.open)
				socket.notify('changed');
		}

		answerFrame() {
			const tab = this.tab();
			return {
				Frame: ++this.frame,
				Title: scenario.title ?? 'Test base',
				Status: 'Ready',
				Menu: scenario.menu ?? {},
				Tabs: { NodeChildren: this.tabs.map((one) => ({ NodeId: one.id, Title: one.title, Locked: one.locked === true,
					...(one.columns !== undefined ? { Kind: homePageKind } : {}) })) },
				ActiveTab: this.active,
				...(tab !== undefined ? { View: this.view(tab) } : {}),
				...(this.request !== undefined ? { Request: this.request } : {}),
			};
		}

		view(tab) {
			if (tab.columns === undefined)
				return this.form(tab);
			// The start page: a form of each cell by its own control ids and Key, as a tab's; a cell with none says why.
			return {
				...(tab.gravity !== undefined ? { Gravity: tab.gravity } : {}),
				Columns: { NodeChildren: tab.columns.map((cells, column) => ({
					NodeId: column + 1,
					NodeChildren: cells.map((cell, place) => ({
						NodeId: place + 1, Title: cell.title, Height: cell.height ?? 1,
						...(cell.table !== undefined
							? { View: this.form({ id: tab.id * 10 + column * 3 + place + 1, ...cell }) } : { Unavailable: cell.unavailable }),
					})),
				})) },
			};
		}

		/** A form, its table remembered under its Key: a read and an event name the form, not the tab. @param {any} tab */
		form(tab) {
			this.forms.set(`form${tab.id}`, tab);
			const table = tab.table;
			const state = { ViewMode: table.viewMode, Version: table.version ?? 1 };
			if (table.currentRow !== undefined)
				state.CurrentRow = table.currentRow;
			if (table.currentColumn !== undefined)
				state.CurrentColumn = table.currentColumn;
			if (table.contextMenu !== undefined)
				state.ContextMenu = { NodeChildren: table.contextMenu };
			if (table.bar !== false)
				state.CommandBar = { NodeChildren: [{ Id: viewModeCommand, Caption: 'View mode' }] };
			return {
				NodeType: 'ClientForm', ControlId: tab.id * 100, Key: `form${tab.id}`,
				State: {},
				// Laid as a saved form lays them: each control in a sizer's item, the table taking what is left. A tab's
				// `controls` are nodes as the server writes them, put first as they are.
				NodeChildren: [...(tab.controls ?? []), ...(tab.fields ?? []).map((field, place) => ({
					NodeType: 'SizerItem', ControlId: 500 + place, Proportion: 0, Stretch: expand,
					NodeChildren: [{
						NodeType: 'Textctrl', ControlId: field.id, ButtonSelect: field.select ?? true, ButtonClear: field.clear ?? true,
						State: { Caption: field.caption, Text: field.text ?? '' },
					}],
				})), {
					NodeType: 'SizerItem', ControlId: 600, Proportion: 1, Stretch: expand,
					NodeChildren: [{
						NodeType: 'Tablebox', ControlId: table.id ?? 1, Header: true, HeaderHeight: table.headerHeight ?? 1,
						Footer: table.footer === true, FooterHeight: table.footerHeight ?? 1, FrezeeRow: 0, FrezeeCol: 0,
						RowSelectionMode: table.selection ?? 1,
						State: state,
						NodeChildren: table.columns,
					}],
				}],
			};
		}

		rowsOf(table, parent) {
			const all = table.rows;
			return table.viewMode === 2 ? all : all.filter((row) => (row.parent ?? 0) === parent);
		}

		/** A read as the server's Fetch: its level, from its Anchor which way, the rows ahead when asked. */
		fetch(table, request) {
			const rows = this.rowsOf(table, request.Parent ?? 0);
			const count = request.Count ?? 100;
			let from = 0;
			let to = 0;
			const at = rows.findIndex((row) => row.id === request.Anchor);
			if ((request.Direction ?? 0) > 0) {
				from = at + 1;
				to = from + count;
			}
			else if ((request.Direction ?? 0) < 0) {
				to = at;
				from = Math.max(0, to - count);
			}
			else {
				const before = request.Before ?? 0;
				const anchor = Math.max(0, at);
				from = Math.max(0, anchor - before);
				to = anchor + count;
			}
			const page = rows.slice(from, to);
			return {
				...(request.Direction === 0 || request.Direction === undefined ? { Before: Math.max(0, at) - from, ...(from === 0 ? { Begin: true } : {}) } : {}),
				...(to >= rows.length ? { End: true } : {}),
				...(table.answerCurrent !== undefined ? { CurrentRow: table.answerCurrent } : {}),
				NodeChildren: page.map((row) => ({
					NodeId: row.id,
					Container: table.viewMode !== 2 && row.container === true,
					ReadOnly: row.readOnly ?? true,
					...(row.group ? { Group: row.group } : {}),
					NodeChildren: Object.entries(row.cells).map(([column, text]) => ({
						NodeId: Number(column), Text: text, ...(row.readOnly === false ? { Value: { t: 'string', v: text } } : {}),
					})),
				})),
			};
		}

		handle(method, params) {
			switch (method) {
			case 'users':
				return { Users: this.users };
			case 'login':
				if (this.users.length > 0 && (!params.User || params.Password !== this.password))
					throw { code: 401, message: 'The name or the password is wrong.' };
				return { Client: 'test', Protocol: params.Protocol, ...this.answerFrame() };
			case 'frame':
			case 'activate':
				if (params.Tab !== undefined)
					this.active = params.Tab;
				return this.answerFrame();
			case 'close':
				if (this.tabs.find((tab) => tab.id === params.Tab)?.unsaved === true) {
					this.request = { Id: this.nextRequest++, Kind: messageRequest, Text: 'Save?', Style: yes | no | cancel };
					return this.answerFrame();
				}
				return this.closed(params.Tab);
			case 'fetch':
				return this.fetch((this.forms.get(params.Form) ?? this.tab()).table, params.Request);
			case 'event':
				return this.event(params);
			case 'respond':
				return this.respond(params);
			case 'schema':
				return scenario.schemas?.[params.Schema] ?? {};
			case 'picture':
				return { Pictures: [] };
			case 'logout':
				return {};
			default:
				throw { code: 400, message: `no method ${method}` };
			}
		}

		event(params) {
			const table = this.tab()?.table;
			if (params.Event === Event.Command && params.Args?.Id === viewModeCommand)
				this.request = { Id: this.nextRequest++, Kind: viewModeRequest, Mode: table.viewMode };
			else if (table !== undefined && scenario.opens !== undefined && params.Event === Event.Open && params.Args?.Column === undefined)
				this.tabs.push({ id: this.tabs.length + 1, ...scenario.opens }), this.active = this.tabs.length;
			return this.answerFrame();
		}

		closed(id) {
			this.tabs = this.tabs.filter((tab) => tab.id !== id);
			this.active = this.tabs.at(-1)?.id ?? 0;
			return this.answerFrame();
		}

		respond(params) {
			const table = this.tab().table;
			this.request = undefined;
			if (params.Response?.Button === no)
				return this.closed(this.active);
			if (params.Response?.Mode !== undefined && params.Response.Mode !== table.viewMode) {
				table.viewMode = params.Response.Mode;
				table.version = (table.version ?? 1) + 1;
			}
			return this.answerFrame();
		}
	}

	class FakeSocket {
		static open = new Set();

		constructor(address) {
			this.address = address;
			this.binaryType = 'arraybuffer';
			this.readyState = 0;
			FakeSocket.open.add(this);
			setTimeout(() => {
				this.readyState = 1;
				this.onopen?.({});
			}, scenario.connectLatency ?? 0);
		}

		async send(bytes) {
			const { decode, encode } = await packet;
			const message = decode(bytes);
			window.fake.calls.push({ method: message.method, params: message.params });
			let reply;
			try {
				reply = { id: message.id, result: window.fake.handle(message.method, message.params ?? {}) };
			}
			catch (refusal) {
				if (refusal.code === undefined)
					throw refusal;
				reply = { id: message.id, error: refusal };
			}
			const server = window.fake;
			const wait = message.method === 'fetch' ? server.fetchLatency : message.method === 'login' ? server.loginLatency : server.latency;
			setTimeout(() => this.onmessage?.({ data: encode(reply) }), wait);
		}

		async notify(method) {
			const { encode } = await packet;
			this.onmessage?.({ data: encode({ method, params: {} }) });
		}

		close(code = 1000, reason = '') {
			FakeSocket.open.delete(this);
			this.onclose?.({ code, reason });
		}
	}

	window.fake = new FakeServer();
	window.WebSocket = FakeSocket;
})();
