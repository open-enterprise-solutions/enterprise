/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  registerTranslation
} from "../chunks/chunk.HOKYDFUG.js";
import "../chunks/chunk.JHZRD2LV.js";

// src/translations/cs.ts
var translation = {
  $code: "cs",
  $name: "\u010Ce\u0161tina",
  $dir: "ltr",
  allTagsRemoved: "V\u0161echny \u0161t\xEDtky odebr\xE1ny",
  am: "dop.",
  autosizeColumn: "P\u0159izp\u016Fsobit \u0161\xED\u0159ku obsahu",
  captions: "Titulky",
  carousel: "Karusel",
  chooseDate: "Vyberte datum",
  chooseDecade: "Vyberte desetilet\xED",
  chooseMonth: "Vyberte m\u011Bs\xEDc",
  chooseTime: "Vyberte \u010Das",
  chooseYear: "Vyberte rok",
  clearEntry: "Smazat polo\u017Eku",
  clearFilter: "Zru\u0161it filtr",
  clearSort: "Zru\u0161it \u0159azen\xED",
  close: "Zav\u0159\xEDt",
  closeCalendar: "Zav\u0159\xEDt kalend\xE1\u0159",
  closeTimeInput: "Zav\u0159\xEDt v\xFDb\u011Br \u010Dasu",
  collapseRow: "Sbalit \u0159\xE1dek",
  columnMenu: "Mo\u017Enosti sloupce",
  columnMovedToPosition: (label, position, total) => `${label} p\u0159esunut na pozici ${position} z ${total}`,
  columns: "Sloupce",
  compactPageXOfY: (page, total) => `${page} z ${total}`,
  completed: "Dokon\u010Deno",
  copied: "Zkop\xEDrov\xE1no",
  copy: "Kop\xEDrovat",
  createOption: (value) => `Vytvo\u0159it "${value}"`,
  currentlyPlaying: "pr\xE1v\u011B se p\u0159ehr\xE1v\xE1",
  currentValue: "Sou\u010Dasn\xE1 hodnota",
  date: "Datum",
  datePickerKeyboardHelp: "Pomoc\xED \u0161ipek zm\u011B\u0148te hodnoty; stisknut\xEDm Alt+\u0160ipka dol\u016F otev\u0159ete kalend\xE1\u0159.",
  day: "Den",
  dayPeriod: "dop./odp.",
  decrement: "Sn\xED\u017Eit",
  deselectAllRows: "Zru\u0161it v\xFDb\u011Br v\u0161ech \u0159\xE1dk\u016F",
  disabled: "Zak\xE1z\xE1no",
  dropFileHere: "Drop file here or click to browse",
  dropFilesHere: "Drop files here or click to browse",
  empty: "Pr\xE1zdn\xE9",
  endDate: "Datum ukon\u010Den\xED",
  enterFullscreen: "P\u0159ej\xEDt na celou obrazovku",
  error: "Chyba",
  exitFullscreen: "Ukon\u010Dit celou obrazovku",
  expandRow: "Rozbalit \u0159\xE1dek",
  filterByColumn: (label) => `Filtrovat podle: ${label}`,
  filterFrom: "Od",
  filterMax: "Max",
  filterMin: "Min",
  filterTo: "Do",
  firstPage: "Prvn\xED str\xE1nka",
  goToSlide: (slide, count) => `P\u0159ej\xEDt na slide ${slide} z ${count}`,
  hideColumn: "Skr\xFDt sloupec",
  hidePassword: "Skr\xFDt heslo",
  hour: "Hodina",
  incompleteDate: "Zadejte platn\xE9 datum.",
  increment: "Zv\xFD\u0161it",
  jumpBackwardX: (count) => {
    if (count === 1) return "P\u0159ej\xEDt o 1 str\xE1nku zp\u011Bt";
    if (count >= 2 && count <= 4) return `P\u0159ej\xEDt o ${count} str\xE1nky zp\u011Bt`;
    return `P\u0159ej\xEDt o ${count} str\xE1nek zp\u011Bt`;
  },
  jumpForwardX: (count) => {
    if (count === 1) return "P\u0159ej\xEDt o 1 str\xE1nku vp\u0159ed";
    if (count >= 2 && count <= 4) return `P\u0159ej\xEDt o ${count} str\xE1nky vp\u0159ed`;
    return `P\u0159ej\xEDt o ${count} str\xE1nek vp\u0159ed`;
  },
  lastPage: "Posledn\xED str\xE1nka",
  loading: "Nahr\xE1v\xE1 se",
  locked: "Uzam\u010Deno",
  minute: "Minuta",
  month: "M\u011Bs\xEDc",
  moreOptions: "Dal\u0161\xED mo\u017Enosti",
  mute: "Ztlumit",
  nextDecade: "Dal\u0161\xED desetilet\xED",
  nextMonth: "Dal\u0161\xED m\u011Bs\xEDc",
  nextPage: "Dal\u0161\xED str\xE1nka",
  nextSlide: "Dal\u0161\xED slide",
  nextVideo: "Dal\u0161\xED video",
  nextYear: "Dal\u0161\xED rok",
  noData: "\u017D\xE1dn\xE1 data",
  noOptions: "\u017D\xE1dn\xE9 mo\u017Enosti",
  noResults: "\u017D\xE1dn\xE9 odpov\xEDdaj\xEDc\xED v\xFDsledky",
  notCompleted: "Nedokon\u010Deno",
  now: "Nyn\xED",
  numCharacters: (num) => {
    if (num === 1) return "1 znak";
    if (num >= 2 && num <= 4) return `${num} znaky`;
    return `${num} znak\u016F`;
  },
  numCharactersRemaining: (num) => {
    if (num === 1) return "1 zb\xFDvaj\xEDc\xED znak";
    if (num >= 2 && num <= 4) return `${num} zb\xFDvaj\xEDc\xED znaky`;
    return `${num} zb\xFDvaj\xEDc\xEDch znak\u016F`;
  },
  numOptionsAvailable: (num) => {
    if (num === 0) return "Nejsou k dispozici \u017E\xE1dn\xE9 mo\u017Enosti";
    if (num === 1) return "K dispozici je 1 mo\u017Enost";
    if (num >= 2 && num <= 4) return `K dispozici jsou ${num} mo\u017Enosti`;
    return `K dispozici je ${num} mo\u017Enost\xED`;
  },
  numOptionsSelected: (num) => {
    if (num === 0) return "Nejsou vybr\xE1ny \u017E\xE1dn\xE9 mo\u017Enosti";
    if (num === 1) return "Je vybr\xE1na jedna mo\u017Enost";
    return `Po\u010Det vybran\xFDch mo\u017Enost\xED: ${num}`;
  },
  numRowsCopied: (num) => {
    if (num === 1) return "Byl zkop\xEDrov\xE1n 1 \u0159\xE1dek";
    if (num >= 2 && num <= 4) return `Byly zkop\xEDrov\xE1ny ${num} \u0159\xE1dky`;
    return `Bylo zkop\xEDrov\xE1no ${num} \u0159\xE1dk\u016F`;
  },
  numRowsSelected: (num) => {
    if (num === 1) return "Je vybr\xE1n 1 \u0159\xE1dek";
    if (num >= 2 && num <= 4) return `Jsou vybr\xE1ny ${num} \u0159\xE1dky`;
    return `Je vybr\xE1no ${num} \u0159\xE1dk\u016F`;
  },
  optionPosition: (label, position, total) => `${label}, ${position} z ${total}`,
  optionsLoadError: "Mo\u017Enosti se nepoda\u0159ilo na\u010D\xEDst",
  pageXOfY: (page, total) => `Str\xE1nka ${page} z ${total}`,
  pagination: "Str\xE1nkov\xE1n\xED",
  pause: "Pozastavit",
  pauseAnimation: "Pozastavit animaci",
  pictureInPicture: "Obraz v obraze",
  pinLeft: "P\u0159ipnout vlevo",
  pinRight: "P\u0159ipnout vpravo",
  play: "P\u0159ehr\xE1t",
  playAnimation: "P\u0159ehr\xE1t animaci",
  playbackSpeed: "Rychlost p\u0159ehr\xE1v\xE1n\xED",
  playlist: "Playlist",
  pm: "odp.",
  previousDecade: "P\u0159edchoz\xED desetilet\xED",
  previousMonth: "P\u0159edchoz\xED m\u011Bs\xEDc",
  previousPage: "P\u0159edchoz\xED str\xE1nka",
  previousSlide: "P\u0159edchoz\xED slide",
  previousVideo: "P\u0159edchoz\xED video",
  previousYear: "P\u0159edchoz\xED rok",
  progress: "Pr\u016Fb\u011Bh",
  rangeTooLong: (max) => {
    if (max === 1) return "Vyberte rozsah nejv\xFD\u0161e 1 den";
    if (max >= 2 && max <= 4) return `Vyberte rozsah nejv\xFD\u0161e ${max} dny`;
    return `Vyberte rozsah nejv\xFD\u0161e ${max} dn\u016F`;
  },
  rangeTooShort: (min) => {
    if (min === 1) return "Vyberte rozsah dlouh\xFD alespo\u0148 1 den";
    if (min >= 2 && min <= 4) return `Vyberte rozsah dlouh\xFD alespo\u0148 ${min} dny`;
    return `Vyberte rozsah dlouh\xFD alespo\u0148 ${min} dn\u016F`;
  },
  readonly: "Jen pro \u010Dten\xED",
  remove: "Odstranit",
  resetColumns: "Obnovit sloupce",
  resize: "Zm\u011Bnit velikost",
  resizeColumn: "Zm\u011Bnit \u0161\xED\u0159ku sloupce",
  rowsPerPage: "\u0158\xE1dk\u016F na str\xE1nku",
  scrollableRegion: "Posunovateln\xE1 oblast",
  scrollToEnd: "Scrollovat na konec",
  scrollToStart: "Scrollovat na za\u010D\xE1tek",
  search: "Hledat",
  second: "Sekunda",
  seek: "P\u0159ej\xEDt",
  seekProgress: (current, duration) => `${current} z ${duration}`,
  selectAColorFromTheScreen: "Vybrat barvu z obrazovky",
  selectAllRows: "Vybrat v\u0161echny \u0159\xE1dky",
  selected: "Vybr\xE1no",
  selectedDateLabel: (date) => `Vybr\xE1no: ${date}`,
  selectedRangeLabel: (range) => `Vybran\xFD rozsah: ${range}`,
  selectGroup: "Vybrat skupinu",
  selectionCleared: "V\xFDb\u011Br zru\u0161en",
  selectRow: "Vybrat \u0159\xE1dek",
  showingNofMRows: (shown, total) => `Zobrazeno ${shown} z ${total} \u0159\xE1dk\u016F`,
  showingXtoYofZ: (start, end, total) => `${start}\u2013${end} z ${total}`,
  showPassword: "Zobrazit heslo",
  slideNum: (slide) => `Slide ${slide}`,
  sortAscending: "Se\u0159adit vzestupn\u011B",
  sortColumn: "Se\u0159adit sloupec",
  sortDescending: "Se\u0159adit sestupn\u011B",
  startDate: "Datum zah\xE1jen\xED",
  steps: "Kroky",
  stepXOfY: (step, total) => `Krok ${step} z ${total}`,
  tagAdded: (tag) => `${tag} p\u0159id\xE1no`,
  tagAlreadyAdded: (tag) => `\u0160t\xEDtek ${tag} ji\u017E existuje`,
  tagInputKeyboardHelp: "Stisknut\xEDm Backspace nebo Delete tento \u0161t\xEDtek odeberete.",
  tagRemoved: (tag) => `${tag} odebr\xE1no`,
  time: "\u010Cas",
  timeInputKeyboardHelp: "Pomoc\xED \u0161ipek zm\u011B\u0148te hodnoty; stisknut\xEDm Alt+\u0160ipka dol\u016F otev\u0159ete v\xFDb\u011Br \u010Dasu.",
  today: "Dnes",
  toggleColorFormat: "P\u0159epnout form\xE1t barvy",
  tooFewTags: (min) => {
    if (min === 1) return "P\u0159idejte alespo\u0148 1 \u0161t\xEDtek";
    if (min >= 2 && min <= 4) return `P\u0159idejte alespo\u0148 ${min} \u0161t\xEDtky`;
    return `P\u0159idejte alespo\u0148 ${min} \u0161t\xEDtk\u016F`;
  },
  tooManyTags: (max) => {
    if (max === 1) return "P\u0159idejte nejv\xFD\u0161e 1 \u0161t\xEDtek";
    if (max >= 2 && max <= 4) return `P\u0159idejte nejv\xFD\u0161e ${max} \u0161t\xEDtky`;
    return `P\u0159idejte nejv\xFD\u0161e ${max} \u0161t\xEDtk\u016F`;
  },
  unmute: "Zapnout zvuk",
  unpin: "Odepnout",
  unpinColumn: "Odepnout sloupec",
  videoPlayer: "P\u0159ehr\xE1va\u010D videa",
  volume: "Hlasitost",
  year: "Rok",
  zoomIn: "P\u0159ibl\xED\u017Eit",
  zoomOut: "Odd\xE1lit"
};
registerTranslation(translation);
var cs_default = translation;
export {
  cs_default as default
};
