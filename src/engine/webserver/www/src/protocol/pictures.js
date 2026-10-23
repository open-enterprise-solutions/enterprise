// THE PICTURES — named by their id everywhere (a backend picture's number, a configuration picture's guid), never
// carried in an answer. The page asks for every id an answer names in one `picture` call before it draws it, and keeps
// them for the session: an id that has none is kept too, so it is not asked again (docs/public/client-protocol.md).

/** @typedef {import('./session.js').Session} Session */

export class Pictures {
	/** @param {Session} session */
	constructor(session) {
		this.session = session;
		/** @type {Map<string, string>} id → a data: URL, or '' when there is none of that id */
		this.known = new Map();
	}

	/**
	 * Every id named in the node and under it (its Picture, Icon and …Picture fields), asked for at once.
	 * @param {any} node
	 */
	async fetchFor(node) {
		const wanted = new Set();
		collect(node, wanted);
		for (const id of this.known.keys())
			wanted.delete(id);
		if (wanted.size === 0)
			return;
		const answer = await this.session.call('picture', { Pictures: [...wanted] });
		for (const one of answer.Pictures?.NodeChildren ?? answer.Pictures ?? [])
			this.known.set(String(one.Id), one.Picture ? `data:image/png;base64,${one.Picture}` : '');
		for (const id of wanted) {
			if (!this.known.has(id))
				this.known.set(id, '');
		}
	}

	/**
	 * The picture's URL to draw, '' when there is none or it has not come.
	 * @param {string | number | undefined} id
	 */
	url(id) {
		return id === undefined || id === null || id === '' ? '' : this.known.get(String(id)) ?? '';
	}
}

/**
 * @param {any} node
 * @param {Set<string>} into
 */
function collect(node, into) {
	if (Array.isArray(node)) {
		for (const one of node)
			collect(one, into);
		return;
	}
	if (node === null || typeof node !== 'object')
		return;
	for (const [name, value] of Object.entries(node)) {
		if ((name === 'Icon' || name.endsWith('Picture')) && (typeof value === 'string' || typeof value === 'number')) {
			if (value !== '' && value !== 0 && value !== '0')
				into.add(String(value));
		}
		else if (typeof value === 'object')
			collect(value, into);
	}
}
