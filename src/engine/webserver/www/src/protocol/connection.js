// THE CONNECTION — the page's socket to its publication: ws(s)://<host>/<publication>/client, each text message one
// JSON-RPC 2.0 request or its response (docs/public/client-protocol.md, Transports). A call is answered by its id; a
// message with no id is the server's notification (`changed`), handed to whoever listens. The names on the wire are
// src/engine/protocol/protocol.h's, written once there; this file says only what the envelope needs (ibRpc).

/**
 * A refused call: the protocol's code (400, 401, 403, 404, 409, 500 — ibRefusal) and what it says to a person; 0 — the
 * connection dropped under it, the request it left unanswered kept to be sent again as it went.
 */
export class Refusal extends Error {
	/**
	 * @param {number} code
	 * @param {string} message
	 * @param {{id: number, text: string}} [unanswered]
	 */
	constructor(code, message, unanswered) {
		super(message);
		this.code = code;
		this.unanswered = unanswered;
	}
}

export class Connection {
	/** @param {string} address the socket's address, ws:// or wss:// */
	constructor(address) {
		this.address = address;
		/** @type {WebSocket | null} */
		this.socket = null;
		/** @type {Map<number, {resolve: (result: any) => void, reject: (error: Error) => void, text: string}>} */
		this.waiting = new Map();
		this.lastId = 0;
		/** @type {(method: string, params: any) => void} */
		this.onNotification = () => {};
		/** @type {(reason: string) => void} */
		this.onClose = () => {};
	}

	/** The socket of the page's own publication: beside the page, ws:// for http://, wss:// for https://. */
	static beside(location) {
		const address = new URL('client', location.href);
		address.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
		return new Connection(address.href);
	}

	/**
	 * The socket opened — the first time, and again after a drop: the ids go on from the last, never back to 0.
	 * @returns {Promise<void>} open, or why not
	 */
	open() {
		return new Promise((resolve, reject) => {
			const socket = new WebSocket(this.address);
			socket.onopen = () => {
				this.socket = socket;
				resolve();
			};
			socket.onmessage = (message) => this.receive(message.data);
			socket.onclose = (closed) => {
				const reason = closed.reason || `the connection closed (${closed.code})`;
				// One that never opened is a failed open, not a drop.
				if (this.socket !== socket) {
					reject(new Error(reason));
					return;
				}
				this.socket = null;
				// A call still out is answered by nobody now: it is refused, so whoever waits on it hears why — and keeps
				// what it sent, to send it again if the session comes back.
				for (const [id, waiter] of this.waiting)
					waiter.reject(new Refusal(0, reason, { id, text: waiter.text }));
				this.waiting.clear();
				this.onClose(reason);
			};
		});
	}

	/**
	 * One call, answered by its result or refused.
	 * @param {string} method
	 * @param {object} params
	 * @returns {Promise<any>}
	 */
	call(method, params) {
		const id = ++this.lastId;
		return this.exchange({ id, text: JSON.stringify({ jsonrpc: '2.0', id, method, params }) });
	}

	/**
	 * A request sent as it is written and answered by its id — a call, or one a drop left unanswered, sent again under
	 * its own id (the server answers it from its slot if it ran it).
	 * @param {{id: number, text: string}} request
	 * @returns {Promise<any>}
	 */
	exchange(request) {
		if (this.socket === null)
			return Promise.reject(new Refusal(0, 'not connected', request));
		const answered = new Promise((resolve, reject) => this.waiting.set(request.id, { resolve, reject, text: request.text }));
		this.socket.send(request.text);
		return answered;
	}

	close() {
		this.socket?.close();
	}

	/** @param {string} text */
	receive(text) {
		const message = JSON.parse(text);
		// A notification has a method where an answer has its id.
		if (message.method !== undefined) {
			this.onNotification(message.method, message.params ?? {});
			return;
		}
		const waiter = this.waiting.get(message.id);
		if (waiter === undefined)
			return;
		this.waiting.delete(message.id);
		if (message.error !== undefined)
			waiter.reject(new Refusal(message.error.code, message.error.message));
		else
			waiter.resolve(message.result ?? {});
	}
}
