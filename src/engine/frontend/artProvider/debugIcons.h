#pragma once

// The pictures on the designer's debug toolbar, drawn as SVG.
//
// Drawn rather than embedded as PNG/XPM for two reasons: a vector picture is sharp at whatever scale the
// display asks for (a retina toolbar is 2x, a 4K one more), and there is no binary asset to keep in step
// with the other twelve. The toolbar's disabled look is made by the toolbar itself from these, greyed.
//
// One colour language, so a row of them reads at a glance:
//   green  - start something          blue - move a run that is already going (continue, pause, steps)
//   red    - stop / breakpoints       dark grey - the "current line" dot the step arrows point at
//
// 16x16 view box. Each entry is the whole <svg> document.

namespace ibDebugIcons {

#define OES_DBG_SVG_OPEN  "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'>"
#define OES_DBG_SVG_CLOSE "</svg>"

// Green play triangle with a small red dot: "run it AND watch it".
inline constexpr const char* kStart =
	OES_DBG_SVG_OPEN
	"<path d='M3 1.8 L12.6 7.4 L3 13 Z' fill='#2f9e44' stroke='#1f7a31' stroke-width='0.9' stroke-linejoin='round'/>"
	"<circle cx='12.2' cy='12.2' r='3' fill='#e03131' stroke='#ffffff' stroke-width='0.8'/>"
	OES_DBG_SVG_CLOSE;

// Green play triangle, hollow: "just run it".
inline constexpr const char* kStartWithoutDebugging =
	OES_DBG_SVG_OPEN
	"<path d='M3.4 2.4 L13.2 8 L3.4 13.6 Z' fill='#e6f4ea' stroke='#2f9e44' stroke-width='1.6' stroke-linejoin='round'/>"
	OES_DBG_SVG_CLOSE;

// An arrow going into a bar: "connect to something that is already running".
inline constexpr const char* kAttach =
	OES_DBG_SVG_OPEN
	"<path d='M1.5 8 H9.5' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' fill='none'/>"
	"<path d='M6.6 4.6 L10 8 L6.6 11.4' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/>"
	"<rect x='12' y='2.5' width='2.6' height='11' rx='0.8' fill='#495057'/>"
	OES_DBG_SVG_CLOSE;

// A bar and a triangle: run on from the stop.
inline constexpr const char* kContinue =
	OES_DBG_SVG_OPEN
	"<rect x='2' y='2.4' width='2.2' height='11.2' rx='0.6' fill='#1c6fd1'/>"
	"<path d='M6.6 2.4 L14 8 L6.6 13.6 Z' fill='#1c6fd1' stroke='#1c6fd1' stroke-width='0.8' stroke-linejoin='round'/>"
	OES_DBG_SVG_CLOSE;

inline constexpr const char* kPause =
	OES_DBG_SVG_OPEN
	"<rect x='3.2' y='2.4' width='3.2' height='11.2' rx='0.7' fill='#1c6fd1'/>"
	"<rect x='9.6' y='2.4' width='3.2' height='11.2' rx='0.7' fill='#1c6fd1'/>"
	OES_DBG_SVG_CLOSE;

// The dot is the line the runtime stands on; the arrow is where the step takes it.
inline constexpr const char* kStepInto =
	OES_DBG_SVG_OPEN
	"<path d='M8 1.8 V9.4' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' fill='none'/>"
	"<path d='M4.8 6.4 L8 9.6 L11.2 6.4' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/>"
	"<circle cx='8' cy='13' r='1.9' fill='#495057'/>"
	OES_DBG_SVG_CLOSE;

// An arc over the dot: past this line, to the next one at the same level.
inline constexpr const char* kStepOver =
	OES_DBG_SVG_OPEN
	"<path d='M2.6 9.6 C2.6 3.2 12.6 3.2 12.6 9' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' fill='none'/>"
	"<path d='M9.9 7.2 L12.6 9.9 L15 7.4' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/>"
	"<circle cx='8' cy='13' r='1.9' fill='#495057'/>"
	OES_DBG_SVG_CLOSE;

inline constexpr const char* kStepOut =
	OES_DBG_SVG_OPEN
	"<path d='M8 9.6 V2.2' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' fill='none'/>"
	"<path d='M4.8 5.4 L8 2.2 L11.2 5.4' stroke='#1c6fd1' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round' fill='none'/>"
	"<circle cx='8' cy='13' r='1.9' fill='#495057'/>"
	OES_DBG_SVG_CLOSE;

// Hollow: let go of the runtime and leave it running.
inline constexpr const char* kStopDebugging =
	OES_DBG_SVG_OPEN
	"<rect x='3' y='3' width='10' height='10' rx='1.4' fill='#fff5f5' stroke='#e03131' stroke-width='1.7'/>"
	OES_DBG_SVG_CLOSE;

// Solid: end the program being debugged.
inline constexpr const char* kStopProgram =
	OES_DBG_SVG_OPEN
	"<rect x='2.8' y='2.8' width='10.4' height='10.4' rx='1.4' fill='#e03131' stroke='#b02525' stroke-width='0.8'/>"
	OES_DBG_SVG_CLOSE;

// A breakpoint struck through.
inline constexpr const char* kRemoveAllBreakpoints =
	OES_DBG_SVG_OPEN
	"<circle cx='8' cy='8' r='5.4' fill='#e03131' stroke='#b02525' stroke-width='0.8'/>"
	"<path d='M2.2 13.8 L13.8 2.2' stroke='#343a40' stroke-width='2' stroke-linecap='round'/>"
	OES_DBG_SVG_CLOSE;

#undef OES_DBG_SVG_OPEN
#undef OES_DBG_SVG_CLOSE

} // namespace ibDebugIcons
