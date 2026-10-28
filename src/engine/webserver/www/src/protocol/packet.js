// OES-RPC 1.0 — THE PACKET, read and written in the page: the bytes a server's ibDataNode is (src/engine/core/serialize/
// dataBuilder.h — the layout in its header), turned into the objects the page has always read — what
// ibJsonProvider::EmitNode wrote as JSON (src/engine/core/serialize/provider/jsonProvider.cpp) — and back. Nothing in the bytes is
// a place, only lengths: the reader goes by them. Checked as it is walked — a length past its container, a count its
// lengths do not hold, a kind unknown, nesting past 256: it throws, and the packet is not read at all.
//
//   packet = "OES1" · version u16 · flags u16 · length u32 · 0 u32         — the root node at 16
//   node   = length u32 · fields' length u32 · fields u32 · children u32 · raw length u32 · metaId i32 · clsid u64
//            · raw (even) · fields · children
//   field  = kind u8 · 0 u8 · name (length u16, UTF-16LE) · value          — an even size
//
// THE NAMES OF THE TYPES ride beside the result: the root's `types` — a node whose fields are named by a clsid's digits,
// each the name of that type — names the NodeType of every node in the packet.
//
// BYTES ARE BYTES: a Binary — a picture's PNG, a file's part — and a node's raw block are read as a Uint8Array of their
// own (a view into the packet would keep all of it alive in the frame the page holds, and a copy of the frame would copy
// it whole), and a Uint8Array or an ArrayBuffer is written as one; a string is always text.

const MAGIC = [0x4f, 0x45, 0x53, 0x31];   // "OES1"
const VERSION = 1;
const PACKET_HEADER = 16;
const NODE_HEADER = 32;
const MAX_NESTING = 256;

const Kind = Object.freeze({ Empty: 0, Bool: 1, Number: 2, Date: 3, String: 4, Binary: 5, Child: 6, Array: 7, Whole: 8 });

// The names a node's own place in the JSON view had: its type, its id, its raw block, its children.
const NODE_TYPE = 'NodeType';
const NODE_ID = 'NodeId';
const NODE_RAW = 'NodeRaw';
const NODE_CHILDREN = 'NodeChildren';
// …and the root's dictionary of type names.
const TYPES = 'types';

// A Date is milliseconds from 0001-01-01 00:00:00; the page's clock counts from 1970-01-01 — 719162 days later.
const EMPTY_DATE_MS = 62135596800000;

const utf16 = new TextDecoder('utf-16le');

const even = (n) => n + (n & 1);

/**
 * Whether the bytes are a packet (they begin "OES1") — not, say, one deflated.
 * @param {ArrayBuffer | Uint8Array} bytes
 */
export function isPacket(bytes) {
	const view = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
	return view.length >= MAGIC.length && MAGIC.every((byte, place) => view[place] === byte);
}

//*******************************************************************************************
//*                                      Read                                               *
//*******************************************************************************************

/**
 * The packet's root as an object — the JSON view of it, as the page read it before: NodeType (by the packet's `types`),
 * NodeId, the fields in their order, NodeRaw, NodeChildren. Throws when the bytes are no packet.
 * @param {ArrayBuffer | Uint8Array} buffer
 * @returns {any}
 */
export function decode(buffer) {
	const bytes = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
	const reader = new Reader(bytes);
	if (bytes.length < PACKET_HEADER + NODE_HEADER || !isPacket(bytes))
		throw new Error('OES-RPC: the bytes are no packet');
	if (reader.view.getUint16(4, true) !== VERSION)
		throw new Error(`OES-RPC: the packet is version ${reader.view.getUint16(4, true)}, this page reads ${VERSION}`);
	if (reader.view.getUint32(8, true) !== bytes.length)
		throw new Error('OES-RPC: the packet\'s length is not what it says');
	const root = reader.node(PACKET_HEADER, bytes.length, 0, true);
	if (PACKET_HEADER + reader.view.getUint32(PACKET_HEADER, true) !== bytes.length)
		throw new Error('OES-RPC: bytes past the root');
	reader.nameTypes();
	return root;
}

class Reader {
	/** @param {Uint8Array} bytes */
	constructor(bytes) {
		this.bytes = bytes;
		this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
		/** @type {Map<string, string>} the root's `types` — clsid digits to a name */
		this.types = new Map();
		/** @type {Array<[any, bigint]>} the nodes whose type is named once `types` is read: it comes after the result */
		this.typed = [];
	}

	refuse(why, at) {
		throw new Error(`OES-RPC: the bytes are no packet: ${why} at byte ${at}`);
	}

	text(at, units) {
		return utf16.decode(this.bytes.subarray(at, at + 2 * units));
	}

	/** The node at `at`, inside `end`. The root alone keeps `types` for the names, not as a field. */
	node(at, end, depth, root = false) {
		if (depth > MAX_NESTING)
			this.refuse('nesting too deep', at);
		if (at + NODE_HEADER > end)
			this.refuse('a node\'s header past its container', at);
		const view = this.view;
		const length = view.getUint32(at, true);
		if (length < NODE_HEADER || (length & 1) !== 0 || at + length > end)
			this.refuse('a node\'s length out of its container', at);
		const stop = at + length;
		const fieldCount = view.getUint32(at + 8, true);
		const childCount = view.getUint32(at + 12, true);
		const rawLength = view.getUint32(at + 16, true);
		const metaId = view.getInt32(at + 20, true);
		const clsid = view.getBigUint64(at + 24, true);
		const rawAt = at + NODE_HEADER;
		const fieldsAt = rawAt + even(rawLength);
		const childrenAt = fieldsAt + view.getUint32(at + 4, true);
		if (childrenAt > stop)
			this.refuse('a node\'s parts past its length', at);

		const object = {};
		if (clsid !== 0n) {
			object[NODE_TYPE] = undefined;   // its place first, its name once the packet's `types` is read
			this.typed.push([object, clsid]);
		}
		if (metaId !== 0)
			object[NODE_ID] = metaId;

		let place = fieldsAt;
		for (let i = 0; i < fieldCount; i++) {
			const [name, value, next, kind] = this.field(place, childrenAt, depth);
			if (root && name === TYPES && kind === Kind.Child)
				this.keepTypes(value);
			else
				object[name] = value;
			place = next;
		}
		if (place !== childrenAt)
			this.refuse('a node\'s fields not what their length says', at);

		if (rawLength !== 0)
			object[NODE_RAW] = this.bytes.slice(rawAt, rawAt + rawLength);

		if (childCount !== 0) {
			const children = [];
			for (let i = 0; i < childCount; i++) {
				children.push(this.node(place, stop, depth + 1));
				place += view.getUint32(place, true);
			}
			object[NODE_CHILDREN] = children;
		}
		if (place !== stop)
			this.refuse('a node\'s children not what its length says', at);
		return object;
	}

	/** The field at `at`, inside `end`: its name, its value, the place past it, and its kind. */
	field(at, end, depth) {
		if (at + 4 > end)
			this.refuse('a field past its container', at);
		const kind = this.bytes[at];
		const units = this.view.getUint16(at + 2, true);
		const valueAt = at + 4 + 2 * units;
		if (valueAt > end)
			this.refuse('a name past its container', at);
		const name = this.text(at + 4, units);
		const [value, next] = this.value(kind, valueAt, end, depth, at);
		return [name, value, next, kind];
	}

	value(kind, at, end, depth, field) {
		const view = this.view;
		const within = (size) => {
			if (at + size > end)
				this.refuse('a value past its container', field);
			return at + size;
		};
		switch (kind) {
		case Kind.Empty:
			return [null, at];
		case Kind.Bool:
			return [this.bytes[at] !== 0, within(2)];
		case Kind.Whole: {
			const next = within(8);
			return [Number(view.getBigInt64(at, true)), next];
		}
		case Kind.Date: {
			const next = within(8);
			return [dateText(Number(view.getBigInt64(at, true))), next];
		}
		case Kind.Number: {
			within(4);
			const length = view.getUint32(at, true);
			const next = within(4 + even(length));
			return [this.number(at + 4, length, field), next];
		}
		case Kind.String: {
			within(4);
			const units = view.getUint32(at, true);
			const next = within(4 + 2 * units);
			return [this.text(at + 4, units), next];
		}
		case Kind.Binary: {
			within(4);
			const length = view.getUint32(at, true);
			const next = within(4 + even(length));
			return [this.bytes.slice(at + 4, at + 4 + length), next];
		}
		case Kind.Child: {
			const object = this.node(at, end, depth + 1);
			return [object, at + view.getUint32(at, true)];
		}
		case Kind.Array: {
			within(8);
			if (depth + 1 > MAX_NESTING)
				this.refuse('nesting too deep', field);
			const total = view.getUint32(at, true);
			const count = view.getUint32(at + 4, true);
			if (total < 8)
				this.refuse('a list\'s length out of its container', field);
			const stop = within(total);
			const items = [];
			let place = at + 8;
			for (let i = 0; i < count; i++) {
				const [, item, next] = this.field(place, stop, depth + 1);
				items.push(item);
				place = next;
			}
			if (place !== stop)
				this.refuse('a list\'s items not what its length says', field);
			return [items, stop];
		}
		default:
			return this.refuse('an unknown kind', field);
		}
	}

	/** ibNumber's bytes — sign u8 · exp10 i32 · limbs u32 · the limbs, least first — as the JS number its digits are. */
	number(at, length, field) {
		if (length === 0)
			return 0;
		const view = this.view;
		const count = length >= 9 ? view.getUint32(at + 5, true) : -1;
		if (count < 0 || length !== 9 + 4 * count)
			this.refuse('a number\'s bytes not what they say', field);
		let magnitude = 0n;
		for (let i = count - 1; i >= 0; i--)
			magnitude = (magnitude << 32n) | BigInt(view.getUint32(at + 9 + 4 * i, true));
		const sign = this.bytes[at] !== 0 ? '-' : '';
		return Number(`${sign}${magnitude}e${view.getInt32(at + 1, true)}`);
	}

	/** The root's `types`, read: a clsid's digits to its name. */
	keepTypes(types) {
		for (const [clsid, name] of Object.entries(types)) {
			if (typeof name === 'string')
				this.types.set(clsid, name);
		}
	}

	/** Each node's NodeType — its name, as the JSON view wrote it; the clsid itself when the packet does not name it. */
	nameTypes() {
		for (const [object, clsid] of this.typed)
			object[NODE_TYPE] = this.types.get(clsid.toString()) ?? Number(clsid);
	}
}

/** A Date's milliseconds as the JSON view wrote it — "YYYY-MM-DD HH:MM:SS"; the empty date is "". */
function dateText(value) {
	if (value === 0)
		return '';
	const date = new Date(value - EMPTY_DATE_MS);
	const two = (n) => String(n).padStart(2, '0');
	return `${String(date.getUTCFullYear()).padStart(4, '0')}-${two(date.getUTCMonth() + 1)}-${two(date.getUTCDate())} `
		+ `${two(date.getUTCHours())}:${two(date.getUTCMinutes())}:${two(date.getUTCSeconds())}`;
}

/**
 * Bytes as they are written — a Uint8Array over an ArrayBuffer or a view's; null: no bytes.
 * @param {unknown} value
 * @returns {Uint8Array | null}
 */
function bytesOf(value) {
	if (value instanceof ArrayBuffer)
		return new Uint8Array(value);
	if (ArrayBuffer.isView(value))
		return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
	return null;
}

//*******************************************************************************************
//*                                      Write                                              *
//*******************************************************************************************

/**
 * The object as a packet — its root node: NodeType (a number — the clsid; a word stays a field for the server to look
 * up), NodeId, NodeRaw and NodeChildren in the node's own places, every other key a field in its order. What
 * JSON.stringify leaves out is left out: an undefined, a function; toJSON is asked.
 * @param {object} object
 * @returns {ArrayBuffer}
 */
export function encode(object) {
	const writer = new Writer();
	writer.reserve(PACKET_HEADER);
	writer.node(object ?? {}, 0);
	const length = writer.length;
	MAGIC.forEach((byte, place) => { writer.bytes[place] = byte; });
	writer.view.setUint16(4, VERSION, true);
	writer.view.setUint16(6, 0, true);
	writer.view.setUint32(8, length, true);
	writer.view.setUint32(12, 0, true);
	return writer.bytes.buffer.slice(0, length);
}

class Writer {
	constructor() {
		this.bytes = new Uint8Array(1024);
		this.view = new DataView(this.bytes.buffer);
		this.length = 0;
	}

	/** `size` bytes at the end, zeroed — their place. */
	reserve(size) {
		const at = this.length;
		const need = at + size;
		if (need > this.bytes.length) {
			const grown = new Uint8Array(Math.max(need, this.bytes.length * 2));
			grown.set(this.bytes.subarray(0, at));
			this.bytes = grown;
			this.view = new DataView(grown.buffer);
		}
		else {
			this.bytes.fill(0, at, need);
		}
		this.length = need;
		return at;
	}

	units(text) {
		const at = this.reserve(2 * text.length);
		for (let i = 0; i < text.length; i++)
			this.view.setUint16(at + 2 * i, text.charCodeAt(i), true);
	}

	octets(data) {
		const at = this.reserve(even(data.length));
		this.bytes.set(data, at);
	}

	node(object, depth) {
		if (depth > MAX_NESTING)
			throw new Error('OES-RPC: nesting too deep to write');
		const at = this.reserve(NODE_HEADER);

		let clsid = 0n;
		const type = object[NODE_TYPE];
		const typed = typeof type === 'number' && Number.isFinite(type);
		if (typed)
			clsid = BigInt.asUintN(64, BigInt(Math.trunc(type)));
		const metaId = typeof object[NODE_ID] === 'number' ? object[NODE_ID] | 0 : 0;

		let rawLength = 0;
		const raw = bytesOf(object[NODE_RAW]);
		if (raw !== null) {
			rawLength = raw.length;
			this.octets(raw);
		}

		const fieldsAt = this.length;
		let fieldCount = 0;
		for (const name of Object.keys(object)) {
			if ((name === NODE_TYPE && typed) || name === NODE_ID || name === NODE_CHILDREN || (name === NODE_RAW && raw !== null))
				continue;
			const value = jsonOf(object[name]);
			if (value === undefined || typeof value === 'function' || typeof value === 'symbol')
				continue;
			this.field(name, value, depth);
			fieldCount++;
		}
		const fieldsLength = this.length - fieldsAt;

		let childCount = 0;
		const children = object[NODE_CHILDREN];
		if (Array.isArray(children)) {
			for (const child of children) {
				const node = jsonOf(child);
				if (node !== null && typeof node === 'object' && !Array.isArray(node)) {
					this.node(node, depth + 1);
					childCount++;
				}
			}
		}

		const v = this.view;   // read now: the buffer grew under the parts
		v.setUint32(at, this.length - at, true);
		v.setUint32(at + 4, fieldsLength, true);
		v.setUint32(at + 8, fieldCount, true);
		v.setUint32(at + 12, childCount, true);
		v.setUint32(at + 16, rawLength, true);
		v.setInt32(at + 20, metaId, true);
		v.setBigUint64(at + 24, clsid, true);
	}

	field(name, value, depth) {
		if (name.length > 0xffff)
			throw new Error('OES-RPC: a name too long to write');
		const at = this.reserve(4);
		this.view.setUint16(at + 2, name.length, true);
		this.units(name);
		// The kind written once the value is: writing it may grow the buffer, and `this.bytes` is read before the right side.
		const kind = this.value(value, depth);
		this.bytes[at] = kind;
	}

	/** A u32 at the end — its place reserved before the view is read: reserving may grow the buffer under it. */
	length32(value) {
		const at = this.reserve(4);
		this.view.setUint32(at, value, true);
	}

	whole(value) {
		const at = this.reserve(8);
		this.view.setBigInt64(at, value, true);
	}

	/** The value's bytes, written; its kind. */
	value(value, depth) {
		if (value === null || value === undefined || typeof value === 'function' || typeof value === 'symbol')
			return Kind.Empty;
		if (typeof value === 'boolean') {
			const at = this.reserve(2);
			this.bytes[at] = value ? 1 : 0;
			return Kind.Bool;
		}
		if (typeof value === 'number') {
			if (!Number.isFinite(value))
				return Kind.Empty;   // as JSON.stringify writes it: null
			if (Number.isSafeInteger(value)) {
				this.whole(BigInt(value));
				return Kind.Whole;
			}
			this.number(String(value));
			return Kind.Number;
		}
		if (typeof value === 'bigint') {
			if (value >= -(1n << 63n) && value < (1n << 63n)) {
				this.whole(value);
				return Kind.Whole;
			}
			this.number(value.toString());
			return Kind.Number;
		}
		if (typeof value === 'string') {
			this.length32(value.length);
			this.units(value);
			return Kind.String;
		}
		const data = bytesOf(value);
		if (data !== null) {
			this.length32(data.length);
			this.octets(data);
			return Kind.Binary;
		}
		if (Array.isArray(value)) {
			if (depth + 1 > MAX_NESTING)
				throw new Error('OES-RPC: nesting too deep to write');
			const at = this.reserve(8);
			for (const item of value)
				this.field('', jsonOf(item) ?? null, depth + 1);   // a hole, an undefined: null, as JSON.stringify has it
			this.view.setUint32(at, this.length - at, true);
			this.view.setUint32(at + 4, value.length, true);
			return Kind.Array;
		}
		this.node(value, depth + 1);
		return Kind.Child;
	}

	/** A number's decimal digits as ibNumber's bytes: sign u8 · exp10 i32 · limbs u32 · the limbs, least first. */
	number(digits) {
		const parsed = /^(-?)(\d+)(?:\.(\d+))?(?:e([+-]?\d+))?$/i.exec(digits);
		if (parsed === null)
			throw new Error(`OES-RPC: ${digits} is no number to write`);
		const fraction = parsed[3] ?? '';
		let magnitude = BigInt(parsed[2] + fraction);
		let exp10 = Number(parsed[4] ?? 0) - fraction.length;
		// Its own zeros trimmed, as ibNumber trims them: one number, one spelling.
		while (magnitude !== 0n && magnitude % 10n === 0n) {
			magnitude /= 10n;
			exp10++;
		}
		const limbs = [];
		for (let rest = magnitude; rest > 0n; rest >>= 32n)
			limbs.push(Number(rest & 0xffffffffn));
		const length = limbs.length === 0 ? 0 : 9 + 4 * limbs.length;   // zero is no bytes at all
		this.length32(length);
		if (length === 0)
			return;
		const at = this.reserve(even(length));
		this.bytes[at] = parsed[1] === '-' ? 1 : 0;
		this.view.setInt32(at + 1, exp10, true);
		this.view.setUint32(at + 5, limbs.length, true);
		limbs.forEach((limb, place) => this.view.setUint32(at + 9 + 4 * place, limb, true));
	}
}

/** What JSON.stringify writes for a value: its toJSON's answer when it has one (a Date — its ISO text). */
function jsonOf(value) {
	return value !== null && typeof value === 'object' && typeof value.toJSON === 'function' ? value.toJSON() : value;
}
