// ABOUT (schema 4) — the thin client's window (frmclient/win/dlgs/about.cpp): the platform and its build, what this session
// runs on, the plugins loaded, whom it thanks. All of it but the thanks is the server's answer. The text can be copied.

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

const schema = 4;

const project = 'https://github.com/open-enterprise-solutions/enterprise';

const thanks = [
	'wxWidgets and wxFormBuilder, Unknown Worlds Entertaiment team',
	'2C team, whose ideas were taken as the basis for building the interpreter',
	'Tomasz Sowa which developed ttmath',
	'Web Awesome and Tabulator, which this page is drawn with',
	'And also everyone who was not mentioned here',
];

/** @param {Session} session */
export async function showAbout(session) {
	const answer = await session.call('schema', { Schema: schema });
	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.label = 'About...';
	dialog.className = 'about';

	const logo = document.createElement('img');
	logo.className = 'logo';
	logo.src = new URL('../../styles/logo.png', import.meta.url).href;
	logo.alt = '';
	const platform = document.createElement('p');
	platform.className = 'platform';
	platform.textContent = `Open Enterprise Solutions, build ${answer.Build ?? 0}`;
	const community = document.createElement('a');
	community.className = 'community';
	community.href = project;
	community.target = '_blank';
	community.rel = 'noopener';
	community.textContent = '(c) 2026 OES community';

	const info = document.createElement('dl');
	// No base, no line for it.
	for (const [label, value] of [['Database', answer.Database], ['Application', answer.Application], ['User', answer.User], ['Locale', answer.Locale]]) {
		if (label === 'Database' && !value)
			continue;
		const term = document.createElement('dt');
		term.textContent = `${label}:`;
		const description = document.createElement('dd');
		description.textContent = value ?? '';
		info.append(term, description);
	}
	dialog.append(logo, platform, community, block('Info', info));

	// Only if any are loaded.
	const plugins = (answer.Plugins?.NodeChildren ?? []).map((/** @type {ProtocolNode} */ plugin) => `${plugin.Name ?? ''}  ${plugin.Version ?? ''}`);
	if (plugins.length > 0)
		dialog.append(block('Plugins', lines(plugins)));
	dialog.append(block('Thanks', lines(thanks)));

	const ok = document.createElement('wa-button');
	ok.slot = 'footer';
	ok.setAttribute('size', 's');
	ok.textContent = 'OK';
	ok.addEventListener('click', () => { dialog.open = false; });
	dialog.append(ok);
	dialog.addEventListener('wa-after-hide', () => dialog.remove());
	document.body.append(dialog);
	dialog.open = true;
}

/**
 * A block under its caption, as the thin client's boxes are.
 * @param {string} caption
 * @param {HTMLElement} content
 */
function block(caption, content) {
	const box = document.createElement('fieldset');
	const legend = document.createElement('legend');
	legend.textContent = caption;
	box.append(legend, content);
	return box;
}

/** @param {string[]} texts */
function lines(texts) {
	const holder = document.createElement('div');
	holder.className = 'lines';
	for (const text of texts) {
		const line = document.createElement('div');
		line.textContent = text;
		holder.append(line);
	}
	return holder;
}
