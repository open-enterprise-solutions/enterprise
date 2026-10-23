// ALL FUNCTIONS (schema 1) — the objects the person may open, grouped by kind, each by its Item, every kind open as the
// thin client's (ExpandAll); one picked is opened as the navigation opens it: schema {Schema 1, Command 1 Open, Args
// {Item, Type}}, Type 100 — the object's default. A configuration has hundreds of them: the search over the tree
// narrows it as it is typed.

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */

const schema = 1;
const open = 1;
const byDefault = 100;

/**
 * @param {Session} session
 * @param {Pictures} pictures
 * @param {(error: Error) => void} refused  a refusal, told to the person
 */
export async function showFunctions(session, pictures, refused) {
	const answer = await session.call('schema', { Schema: schema });
	await pictures.fetchFor(answer).catch(() => {});
	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.label = 'All functions';
	dialog.className = 'functions';
	const tree = document.createElement('wa-tree');
	for (const group of answer.Groups?.NodeChildren ?? []) {
		const branch = /** @type {any} */ (item(group.Title, pictures.url(group.Icon)));
		for (const one of group.NodeChildren ?? [])
			branch.append(item(one.Title, pictures.url(one.Icon), one.Item));
		branch.expanded = true;
		// A kind with nothing the person may open is left out.
		if (branch.querySelector('wa-tree-item') !== null)
			tree.append(branch);
	}
	const search = /** @type {any} */ (document.createElement('wa-input'));
	search.setAttribute('type', 'search');
	search.setAttribute('placeholder', 'Search');
	search.setAttribute('aria-label', 'Search');
	search.setAttribute('autofocus', '');
	const nothing = document.createElement('p');
	nothing.className = 'nothing-found';
	nothing.textContent = 'Nothing found';
	nothing.hidden = true;
	search.addEventListener('input', () => {
		nothing.hidden = narrow(tree, search.value ?? '') > 0;
	});
	tree.addEventListener('wa-selection-change', async (/** @type {any} */ event) => {
		const chosen = event.detail.selection[0];
		if (chosen?.dataset.item === undefined)
			return;
		dialog.open = false;
		await session.call('schema', { Schema: schema, Command: open,
			Args: { Item: Number(chosen.dataset.item), Type: byDefault } }).catch(refused);
	});
	dialog.append(search, tree, nothing);
	dialog.addEventListener('wa-after-hide', () => dialog.remove());
	document.body.append(dialog);
	dialog.open = true;
}

/**
 * The tree narrowed to the objects whose title holds the text, in any case; a kind with none of them is left out, an
 * empty text shows them all. How many are shown.
 * @param {HTMLElement} tree
 * @param {string} text
 * @returns {number}
 */
function narrow(tree, text) {
	const wanted = text.trim().toLocaleLowerCase();
	let found = 0;
	for (const branch of /** @type {HTMLElement[]} */ (Array.from(tree.children))) {
		let shown = 0;
		for (const one of /** @type {NodeListOf<HTMLElement>} */ (branch.querySelectorAll('wa-tree-item'))) {
			one.hidden = !(one.textContent ?? '').toLocaleLowerCase().includes(wanted);
			if (!one.hidden)
				shown++;
		}
		branch.hidden = shown === 0;
		found += shown;
	}
	return found;
}

/**
 * @param {string} title
 * @param {string} picture
 * @param {number} [itemId]
 */
function item(title, picture, itemId) {
	const node = document.createElement('wa-tree-item');
	if (picture) {
		const image = document.createElement('img');
		image.src = picture;
		image.alt = '';
		node.append(image);
	}
	node.append(title ?? '');
	if (itemId !== undefined)
		node.dataset.item = String(itemId);
	return node;
}
