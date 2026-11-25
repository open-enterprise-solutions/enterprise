// ABOUT (schema 4) — the thin client's window (frmclient/win/dlgs/about.cpp), drawn the same way: the program and the
// platform's version beside the logo, the facts of this session, what the platform is built on and whose ideas it grew
// from, the licence. All the facts but the system are the server's answer. Copy puts them on the clipboard as text.

import { _, format, labelText } from '../localization.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

const schema = 4;

const project = 'https://github.com/open-enterprise-solutions/enterprise';

// What the page and the engine behind it are built on — NOTICE.md's components, the page's own drawing first. Names, as
// their projects say them.
const builtOn = ['Web Awesome', 'Tabulator', 'Mbed TLS', 'Asio', 'llhttp', 'Expat', 'LodePNG', 'lunasvg', 'SQLite',
	'Firebird', 'PostgreSQL'];

/** @param {Session} session */
export async function showAbout(session) {
	const answer = await session.call('schema', { Schema: schema });
	const program = `OES ${_('Application Workspace')}`;
	const version = format(_('Version %s, build %d'), answer.Version ?? '', answer.Build ?? 0);

	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.label = format(_('About %s'), program);
	dialog.className = 'about';

	// The program beside the logo.
	const head = document.createElement('div');
	head.className = 'head';
	const logo = document.createElement('img');
	logo.className = 'logo';
	logo.src = new URL('../../styles/logo.png', import.meta.url).href;
	logo.alt = '';
	const title = document.createElement('div');
	title.append(paragraph('program', program), paragraph('version', version),
		paragraph('purpose', _('Business applications from metadata and a language of their own')));
	head.append(logo, title);

	// The facts — a dash for one that is empty; no base, no row for it; the plugins only when any are loaded.
	const plugins = (answer.Plugins?.NodeChildren ?? [])
		.map((/** @type {ProtocolNode} */ plugin) => `${plugin.Name ?? ''} ${plugin.Version ?? ''}`).join(', ');
	/** @type {[string, string][]} */
	const facts = [];
	if (answer.Database)
		facts.push([_('Database'), answer.Database]);
	facts.push([_('Connection'), answer.Program ?? ''], [_('User'), answer.User ?? ''], [_('Language'), answer.Locale ?? ''],
		[_('System'), /** @type {any} */ (navigator).userAgentData?.platform || navigator.platform || '']);
	if (plugins)
		facts.push([_('Plugins'), plugins]);
	const info = document.createElement('dl');
	for (const [label, value] of facts) {
		const term = document.createElement('dt');
		term.textContent = label;
		const description = document.createElement('dd');
		description.textContent = value || '—';
		info.append(term, description);
	}

	// What it is built on, whose ideas it grew from, the licence.
	const credits = document.createElement('div');
	credits.className = 'credits';
	const link = document.createElement('a');
	link.href = project;
	link.target = '_blank';
	link.rel = 'noopener';
	link.textContent = 'github.com/open-enterprise-solutions/enterprise';
	credits.append(paragraph('caption', _('Built on')), paragraph('', builtOn.join(' · ')),
		paragraph('origins', _('Grown from wxFormBuilder (the form editor) and the ideas of 2C (the interpreter)')),
		paragraph('licence', '© 2026 Open Enterprise Solutions · PolyForm Noncommercial 1.0.0'), link);

	dialog.append(head, info, credits);

	const copy = document.createElement('wa-button');
	copy.slot = 'footer';
	copy.setAttribute('size', 's');
	copy.setAttribute('variant', 'neutral');
	copy.textContent = _('Copy');
	copy.addEventListener('click', () => {
		const text = [program, version, ...facts.map(([label, value]) => `${label}: ${value || '—'}`)].join('\n');
		navigator.clipboard?.writeText(text).catch(() => {});
	});
	const ok = document.createElement('wa-button');
	ok.slot = 'footer';
	ok.setAttribute('size', 's');
	ok.textContent = labelText('&OK');
	ok.addEventListener('click', () => { dialog.open = false; });
	dialog.append(copy, ok);
	dialog.addEventListener('wa-after-hide', () => dialog.remove());
	document.body.append(dialog);
	dialog.open = true;
}

/**
 * A line of the window's text.
 * @param {string} className
 * @param {string} text
 */
function paragraph(className, text) {
	const line = document.createElement('p');
	if (className)
		line.className = className;
	line.textContent = text;
	return line;
}
