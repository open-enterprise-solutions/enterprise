// THE THEME — how the interface is painted: light, dark, or as the machine is set. The client's own, kept in this
// browser (Max, 2026-11-24: "nothing is kept on the backend, the client decides as it starts") - the server places it in
// Tools (schema 20, Style) and answers nothing of it; the thin client keeps its own on its machine the same way. Applied
// as the page starts, before anything is drawn, and anew when another is chosen (Tools → Theme...). The page is painted
// by data-theme on <html> (styles/tokens.css) and Web Awesome's own components by its class, wa-dark or wa-light.

import { _, labelText } from '../localization.js';

// The menu item's schema number (ibSchema::ibKind::Style).
export const styleSchema = 20;

// The three, numbered as the thin client keeps them (wx's Appearance).
export const Theme = Object.freeze({ System: 0, Light: 1, Dark: 2 });

// Said when drawn, in the person's language.
const choices = () => [
	[Theme.System, _('As the system is set')],
	[Theme.Light, _('Light')],
	[Theme.Dark, _('Dark')],
];

const keptKey = 'theme';

const systemDark = matchMedia('(prefers-color-scheme: dark)');
// Printing: paper is white whatever the theme - our tokens are the screen's alone (styles/tokens.css), and Web
// Awesome's components are given its light class for as long as the page is printed.
const printing = matchMedia('print');

// The theme in force - the one chosen; the system's is followed as the machine changes it.
let chosen = Theme.System;

/** The page painted as the theme says; the system's, as the machine is set now. @param {number} theme */
function applyTheme(theme) {
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

/** The theme chosen in this browser - the system's when none is, or the storage cannot be read. @returns {number} */
function readKept() {
	try {
		return Number(localStorage.getItem(keptKey) ?? Theme.System);
	}
	catch {
		return Theme.System;
	}
}

/** The page painted in the theme chosen in this browser - as it starts, before anything is drawn. */
export function applyKeptTheme() {
	applyTheme(readKept());
}

/**
 * Tools → Theme...: the three, the one in force chosen. Another chosen is kept in this browser and painted at once; a
 * storage that would not keep it paints it for this page alone.
 */
export async function showTheme() {
	const current = chosen;
	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.label = _('Theme');
	dialog.className = 'theme';
	const list = document.createElement('div');
	list.className = 'themes';
	for (const [value, caption] of choices()) {
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
	ok.textContent = labelText('&OK');
	const cancel = document.createElement('wa-button');
	cancel.slot = 'footer';
	cancel.setAttribute('size', 's');
	cancel.textContent = _('Cancel');
	dialog.append(ok, cancel);

	/** @type {Promise<void>} */
	const done = new Promise((resolve) => {
		ok.addEventListener('click', () => {
			const picked = Number(/** @type {HTMLInputElement | null} */ (list.querySelector('input:checked'))?.value ?? current);
			if (picked !== current) {
				try {
					localStorage.setItem(keptKey, String(picked));
				}
				catch {
					// Not kept: painted for this page, and the next one starts as the machine is set.
				}
				applyTheme(picked);
			}
			dialog.open = false;
		});
		cancel.addEventListener('click', () => { dialog.open = false; });
		dialog.addEventListener('wa-after-hide', (event) => {
			if (event.target !== dialog)
				return;
			dialog.remove();
			resolve();
		});
	});
	document.body.append(dialog);
	dialog.open = true;
	await done;
}
