#ifndef __SPLASH_LOGO_H__
#define __SPLASH_LOGO_H__

// The splash screen, the designer's and the application's alike: an SVG, drawn at the size it is made at. The letters
// are shapes, not text — a font differs from machine to machine, a shape does not — on one line, 104 to 130, the round
// O and S over it by 0.4 at each side, each
// stroke 6; the S two arcs, as the O is a ring (2026-11-24, Max: "the OES text is uneven" - its S, a stroked curve,
// stood 1.2 above the line and 1.8 below it).

static const wxSize s_splashLogoSize(434, 245);
static const wxString s_splashLogo_svg =
	"<svg xmlns='http://www.w3.org/2000/svg' width='434' height='245' viewBox='0 0 434 245'><defs><linearGradient id='ground' x1='0' y1='0' x2='0' y2='1'><stop offset='0' stop-color='#1c9bd8'/><stop offset='1' stop-color='#1682be'/></linearGradient></defs><rect width='434' height='245' fill='url(#ground)'/><rect x='34' y='34' width='366' height='177' rx='64' fill='#ffffff' fill-opacity='0.05'/><circle cx='321' cy='117' r='51' fill='#d4ebf8'/><circle cx='321' cy='117' r='42' fill='#ffe600'/><circle cx='300' cy='117' r='10.4' fill='none' stroke='#5fa9d4' stroke-width='6'/><path d='M316 104 H334 V109.6 H322 V114.2 H332 V119.8 H322 V124.4 H334 V130 H316 Z' fill='#5fa9d4'/><path d='M351.53 109.2 A6.5 5.2 0 1 0 345.9 117 A6.5 5.2 0 1 1 340.27 124.8' fill='none' stroke='#5fa9d4' stroke-width='6'/></svg>";

#endif
