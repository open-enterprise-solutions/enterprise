// THE SESSION — a person's client of the base (docs/public/client-protocol.md). It logs in, keeps the frame it was
// last answered with and each tab's view, and makes every call with what the protocol asks of it: its Client, the
// frame it holds (Since), the state events held since the last call (Pending). One call at a time — the protocol
// answers a client's calls in turn, and a call made before the last one is answered waits for it here.
//
// What it hears it tells as events: 'frame' — the frame changed (the window draws it); 'events' — what happened
// beside the frame (Messages, Clear, Exit, Properties); 'busy' — a call went out with none before it (true), or the
// last one out was answered (false); 'connection' — the connection dropped (false) or came back (true); 'ended' — the
// session is gone.
//
// A DROPPED CONNECTION is not the session's end while the server keeps it (the resume window, docs/public/
// client-protocol.md, A dropped connection): the socket is opened again and the session taken back with `login
// {Token}`, the call the drop left unanswered sent again under its own id — as the thin client's communicator does
// (ibCommunicator::Resume).

import { Refusal } from './connection.js';
import { applyPatch } from './patch.js';

/** @typedef {import('./patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('./connection.js').Connection} Connection */

/** The newest protocol this page speaks. */
const protocol = 6;

// What comes beside a frame and is not the frame: the events, and the login's own answer.
const besideFrame = ['Messages', 'Clear', 'Exit', 'Properties', 'Client', 'Protocol', 'Features', 'Token', 'Read', 'Locale',
	'Since', 'Patch'];

// The state events — where the person is, held until the next call unless the control is subscribed to them
// (ibEvent::IsStateEvent): Focus, Input, Move, Row, Resize.
const stateEvents = new Set([1, 4, 11, 12, 15]);

// What the page accepts of the server (ibLogin::FeatureResume): a session kept through a dropped connection.
const resumeFeature = 'resume';
// How long a dropped connection is tried again: the longest window a server keeps (30 minutes).
const resumeFor = 30 * 60 * 1000;

export class Session extends EventTarget {
	/** @param {Connection} connection */
	constructor(connection) {
		super();
		this.connection = connection;
		/** @type {string | null} */
		this.client = null;
		/** @type {ProtocolNode | null} the frame held — its View the active tab's */
		this.frame = null;
		/** @type {Map<number, ProtocolNode>} the view last drawn for each tab */
		this.views = new Map();
		/** @type {ProtocolNode[]} the state events waiting for the next call */
		this.held = [];
		/** @type {Promise<any>} the call before — the next waits for it */
		this.queue = Promise.resolve();
		/** the calls made and not yet answered */
		this.outstanding = 0;
		/** @type {string | null} the bearer the session is taken back with after a drop — none when the server keeps none */
		this.token = null;
		/** @type {Promise<boolean> | null} the session being taken back */
		this.resuming = null;
		connection.onNotification = (method) => {
			if (method === 'changed')
				this.call('frame', {}).catch(() => {});
		};
		connection.onClose = (reason) => this.dropped(reason);
	}

	/** The names the login window offers — asked before the login, with no Client. */
	async users() {
		const answer = await this.connection.call('users', {});
		return answer.Users ?? [];
	}

	/**
	 * @param {string} user
	 * @param {string} password
	 */
	async login(user, password) {
		const answer = await this.connection.call('login', { User: user, Password: password, Protocol: protocol,
			Features: [resumeFeature] });
		this.client = answer.Client;
		// The bearer comes once — and only from a server that offers to keep the session.
		this.token = (answer.Features ?? []).includes(resumeFeature) ? answer.Token ?? null : null;
		this.take(answer);
		return answer;
	}

	/**
	 * The session ended with the page (closed, reloaded — a page loaded again logs in anew): torn down by the server now,
	 * with whatever it still held open and locked, not when it stops waiting for the page to come back — as the thin
	 * client's window logs out as it goes. Sent at once, past the calls queued: there is nobody to wait for its answer.
	 */
	logout() {
		if (this.client === null)
			return;
		this.token = null;
		this.connection.call('logout', { Client: this.client }).catch(() => {});
	}

	/**
	 * The connection dropped: the session taken back while the server keeps it — what the server changed meanwhile read
	 * then, its `changed` having gone nowhere — or ended.
	 * @param {string} reason
	 */
	async dropped(reason) {
		if (this.token !== null) {
			this.dispatchEvent(new CustomEvent('connection', { detail: false }));
			if (await this.resume()) {
				this.dispatchEvent(new CustomEvent('connection', { detail: true }));
				this.call('frame', {}).catch(() => {});
				return;
			}
		}
		this.dispatchEvent(new CustomEvent('ended', { detail: reason }));
	}

	/**
	 * The session taken back: the socket opened again — tried while the longest window lasts, more seldom as it goes —
	 * then `login {Token}`; the same Client is the same session. One at a time: the drop's own and the call's it left
	 * unanswered are one. False when the session is gone.
	 * @returns {Promise<boolean>}
	 */
	resume() {
		if (this.token === null)
			return Promise.resolve(false);
		this.resuming ??= (async () => {
			const began = Date.now();
			for (let wait = 0; ; wait = Math.min(Math.max(wait * 2, 500), 15000)) {
				await new Promise((resolve) => setTimeout(resolve, wait));
				try {
					await this.connection.open();
					break;
				}
				catch {
					if (Date.now() - began > resumeFor)
						return false;
				}
			}
			try {
				const answer = await this.connection.call('login', { Token: this.token, Protocol: protocol });
				return answer.Client === this.client;
			}
			catch {
				return false;
			}
		})().finally(() => {
			this.resuming = null;
		});
		return this.resuming;
	}

	/**
	 * A call of the logged-in client: its Client, the frame it holds, what is held — then the answer taken in.
	 * @param {string} method
	 * @param {object} params
	 * @returns {Promise<any>}
	 */
	call(method, params) {
		if (this.outstanding++ === 0)
			this.dispatchEvent(new CustomEvent('busy', { detail: true }));
		const next = this.queue.then(async () => {
			const pending = this.held;
			this.held = [];
			const full = { Client: this.client, ...params };
			if (this.frame !== null)
				full.Since = this.frame.Frame;
			if (pending.length > 0)
				full.Pending = pending;
			try {
				let answer;
				try {
					answer = await this.connection.call(method, full);
				}
				catch (error) {
					// The connection dropped under the call: the session taken back, the call sent again as it went.
					if (!(error instanceof Refusal) || error.unanswered === undefined || !await this.resume())
						throw error;
					answer = await this.connection.exchange(error.unanswered);
				}
				this.take(answer);
				return answer;
			}
			catch (error) {
				// No such client, or its session gone: there is nothing to call any more.
				if (error instanceof Refusal && error.code === 401)
					this.dispatchEvent(new CustomEvent('ended', { detail: error.message }));
				throw error;
			}
		});
		this.queue = next.catch(() => {});
		next.catch(() => {}).finally(() => {
			if (--this.outstanding === 0)
				this.dispatchEvent(new CustomEvent('busy', { detail: false }));
		});
		return next;
	}

	/**
	 * What the person did with a control. A state event waits for the next call unless the control's State says the
	 * server is subscribed to it; anything else goes at once, and its answer is the frame.
	 * @param {{Control: number, Event: number, Args?: object, Form?: string, Tab?: number}} event
	 * @param {number[]} [subscribed] the control's State's Subscribed
	 */
	send(event, subscribed = []) {
		if (stateEvents.has(event.Event) && !subscribed.includes(event.Event)) {
			this.hold(event);
			return Promise.resolve(null);
		}
		return this.call('event', event);
	}

	/**
	 * Held: the latest per control and event — Move and Resize per column — in the order last done.
	 * @param {{Control: number, Event: number, Args?: any}} event
	 */
	hold(event) {
		const column = (one) => (one.Event === 11 || one.Event === 15) ? one.Args?.Column : undefined;
		this.held = this.held.filter((one) => one.Control !== event.Control || one.Event !== event.Event
			|| column(one) !== column(event));
		this.held.push(event);
	}

	/**
	 * The answer taken in: a whole frame replaces the one held, a patch is applied to it; what happened beside it is
	 * told apart.
	 * @param {any} answer
	 */
	take(answer) {
		const events = {};
		for (const name of ['Messages', 'Clear', 'Exit', 'Properties']) {
			if (answer[name] !== undefined)
				events[name] = answer[name];
		}
		if (answer.Frame !== undefined) {
			if (answer.Patch !== undefined && this.frame !== null)
				this.takePatch(answer.Patch, answer.Frame);
			else
				this.takeWhole(answer);
			this.dispatchEvent(new CustomEvent('frame', { detail: this.frame }));
		}
		if (Object.keys(events).length > 0)
			this.dispatchEvent(new CustomEvent('events', { detail: events }));
	}

	/** @param {any} answer */
	takeWhole(answer) {
		const frame = { ...answer };
		for (const name of besideFrame)
			delete frame[name];
		this.frame = frame;
		// The store begins again with a frame sent whole.
		this.views.clear();
		if (frame.View !== undefined)
			this.views.set(frame.ActiveTab, frame.View);
	}

	/**
	 * From protocol 2 a patch's View is taken from the view the client was last sent for the tab active now: the view
	 * of the tab the patch makes active is put in place — the one held stays when there is none for that tab — and
	 * the patch's View applied to it.
	 * @param {ProtocolNode} patch
	 * @param {number} number the frame it makes
	 */
	takePatch(patch, number) {
		const frame = /** @type {ProtocolNode} */ (this.frame);
		const active = patch.ActiveTab ?? frame.ActiveTab;
		const view = this.views.get(active);
		if (view !== undefined)
			frame.View = view;
		// A tab with no view of its own yet is patched from a copy of the one on show: that one is another tab's, and
		// the patch must not write into it.
		else if (active !== frame.ActiveTab && frame.View !== undefined)
			frame.View = structuredClone(frame.View);
		applyPatch(frame, patch);
		frame.Frame = number;
		if (frame.View !== undefined)
			this.views.set(frame.ActiveTab, frame.View);
		// A tab closed takes its view with it.
		const open = new Set((frame.Tabs?.NodeChildren ?? []).map((tab) => tab.NodeId));
		for (const tab of this.views.keys()) {
			if (!open.has(tab))
				this.views.delete(tab);
		}
	}
}
