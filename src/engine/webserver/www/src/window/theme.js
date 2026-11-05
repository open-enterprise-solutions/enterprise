// THE THEME (schema 20, Style) — how the person's interface is painted: light, dark, or as their machine is set. Theirs,
// kept in the base, so it goes with them from one machine to the next; read once, as the window comes up, and kept
// anew when they choose another (Tools → Theme...). The page is painted by data-theme on <html> (styles/tokens.css) and
// Web Awesome's own components by its class, wa-dark or wa-light.

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// The schema's number (ibSchema::ibKind::Style).
export const styleSchema = 20;
const schema = styleSchema;
const save = 4;

// ibStyle::ibTheme.
export const Theme = Object.freeze({ System: 0, Light: 1, Dark: 2 });

const choices = [
	[Theme.System, 'As the system is set'],
	[Theme.Light, 'Light'],
	[Theme.Dark, 'Dark'],
];

const systemDark = matchMedia('(prefers-color-scheme: dark)');
// Printing: paper is white whatever the theme - our tokens are the screen's alone (styles/tokens.css), and Web
// Awesome's components are given its light class for as long as the page is printed.
const printing = matchMedia('print');

// The theme in force - the person's choice; the system's is followed as the machine changes it.
let chosen = Theme.System;

/** The page painted as the theme says; the system's, as the machine is set now. @param {number} theme */
export function applyTheme(theme) {
	chosen = theme === Theme.Light || theme === Theme.Dark ? theme : Theme.System;
	const dark = chosen === Theme.Dark || (chosen === Theme.System && systemDark.matches);
	const root = document.documentElement;
	root.dataset.theme = dark ? 'dark' : 'light';
	const darkComponents = dark && !printing.matches;
	root.classList.toggle('wa-dark', darkComponents);
	root.classList.toggle('wa-light', !darkComponents);
}

systemDark.addEventListener('change', () => {
	if (chosen === Theme.System)
		applyTheme(Theme.System);
});
printing.addEventListener('change', () => applyTheme(chosen));
addEventListener('beforeprint', () => applyTheme(chosen));
addEventListener('afterprint', () => applyTheme(chosen));

/**
 * The person's theme, read as the window comes up - when the server offers the schema (the frame's Schemas). A read
 * that fails leaves the page as the machine is set: a look is no reason to stop a login.
 * @param {Session} session
 * @param {ProtocolNode | null} frame
 */
export async function readTheme(session, frame) {
	if (!(frame?.Schemas ?? []).includes(schema))
		return;
	try {
		const answer = await session.call('schema', { Schema: schema });
		applyTheme(Number(answer.Theme ?? Theme.System));
	}
	catch {
		applyTheme(Theme.System);
	}
}

/**
 * Tools → Theme...: the three, the one in force chosen. Another chosen is painted at once and kept by the server as the
 * person's.
 * @param {Session} session
 */
export async function showTheme(session) {
	const answer = await session.call('schema', { Schema: schema });
	const current = Number(answer.Theme ?? Theme.System);
	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.label = 'Theme';
	dialog.className = 'theme';
	const list = document.createElement('div');
	list.className = 'themes';
	for (const [value, caption] of choices) {
		const label = document.createElement('label');
		const radio = document.createElement('input');
		radio.type = 'radio';
		radio.name = 'theme';
		radio.value = String(value);
		radio.checked = value === current;
		label.append(radio, ` ${caption}`);
		list.append(label);
	}
	dialog.append(list);

	const ok = document.createElement('wa-button');
	ok.slot = 'footer';
	ok.setAttribute('size', 's');
	ok.setAttribute('variant', 'brand');
	ok.textContent = 'OK';
	const cancel = document.createElement('wa-button');
	cancel.slot = 'footer';
	cancel.setAttribute('size', 's');
	cancel.textContent = 'Cancel';
	dialog.append(ok, cancel);

	// OK keeps the choice first and closes after: a refusal is then the menu's to show, and a second press while the
	// first is on its way is no second save.
	/** @type {unknown} */
	let refused = null;
	/** @type {Promise<void>} */
	const done = new Promise((resolve, reject) => {
		ok.addEventListener('click', async () => {
			if (ok.hasAttribute('loading'))
				return;
			const picked = Number(/** @type {HTMLInputElement | null} */ (list.querySelector('input:checked'))?.value ?? current);
			if (picked !== current) {
				ok.setAttribute('loading', '');
				try {
					await session.call('schema', { Schema: schema, Command: save, Args: { Theme: picked } });
				}
				catch (error) {
					refused = error ?? new Error('The theme could not be kept');
				}
				ok.removeAttribute('loading');
			}
			// What is kept is painted - and the one in force too, when the page shows another (a read that failed).
			if (!refused)
				applyTheme(picked);
			dialog.open = false;
		});
		cancel.addEventListener('click', () => { dialog.open = false; });
		// Escape, or a click beside it, while the save is on its way: it stays, so its answer is not lost.
		dialog.addEventListener('wa-hide', (event) => {
			if (event.target === dialog && ok.hasAttribute('loading'))
				event.preventDefault();
		});
		dialog.addEventListener('wa-after-hide', (event) => {
			if (event.target !== dialog)
				return;
			dialog.remove();
			if (refused)
				reject(refused);
			else
				resolve();
		});
	});
	document.body.append(dialog);
	dialog.open = true;
	await done;
}
