/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import "../chunks/chunk.DL7HG4WM.js";
import "../chunks/chunk.D5HA4OFX.js";
import {
  registerTranslation
} from "../chunks/chunk.HOKYDFUG.js";
import "../chunks/chunk.JHZRD2LV.js";

// src/translations/es.ts
var translation = {
  $code: "es",
  $name: "Espa\xF1ol",
  $dir: "ltr",
  allTagsRemoved: "Se eliminaron todas las etiquetas",
  am: "AM",
  autosizeColumn: "Ajustar el tama\xF1o de la columna al contenido",
  captions: "Subt\xEDtulos",
  carousel: "Carrusel",
  chooseDate: "Elegir fecha",
  chooseDecade: "Elegir d\xE9cada",
  chooseMonth: "Elegir mes",
  chooseTime: "Elegir hora",
  chooseYear: "Elegir a\xF1o",
  clearEntry: "Borrar entrada",
  clearFilter: "Borrar filtro",
  clearSort: "Borrar orden",
  close: "Cerrar",
  closeCalendar: "Cerrar calendario",
  closeTimeInput: "Cerrar selector de hora",
  collapseRow: "Contraer fila",
  columnMenu: "Opciones de columna",
  columnMovedToPosition: (label, position, total) => `${label} movida a la posici\xF3n ${position} de ${total}`,
  columns: "Columnas",
  compactPageXOfY: (page, total) => `${page} de ${total}`,
  completed: "Completado",
  copied: "Copiado",
  copy: "Copiar",
  createOption: (value) => `Crear "${value}"`,
  currentlyPlaying: "reproduciendo actualmente",
  currentValue: "Valor actual",
  date: "Fecha",
  datePickerKeyboardHelp: "Use las teclas de flecha para cambiar los valores; presione Alt+Flecha abajo para abrir el calendario.",
  day: "D\xEDa",
  dayPeriod: "AM/PM",
  decrement: "Disminuir",
  deselectAllRows: "Deseleccionar todas las filas",
  disabled: "Deshabilitado",
  dropFileHere: "Drop file here or click to browse",
  dropFilesHere: "Drop files here or click to browse",
  empty: "Vac\xEDo",
  endDate: "Fecha de fin",
  enterFullscreen: "Entrar en pantalla completa",
  error: "Error",
  exitFullscreen: "Salir de pantalla completa",
  expandRow: "Expandir fila",
  filterByColumn: (label) => `Filtrar por ${label}`,
  filterFrom: "Desde",
  filterMax: "M\xE1x",
  filterMin: "M\xEDn",
  filterTo: "Hasta",
  firstPage: "Primera p\xE1gina",
  goToSlide: (slide, count) => `Ir a la diapositiva ${slide} de ${count}`,
  hideColumn: "Ocultar columna",
  hidePassword: "Ocultar contrase\xF1a",
  hour: "Hora",
  incompleteDate: "Introduzca una fecha v\xE1lida.",
  increment: "Aumentar",
  jumpBackwardX: (count) => {
    if (count === 1) return "Retroceder 1 p\xE1gina";
    return `Retroceder ${count} p\xE1ginas`;
  },
  jumpForwardX: (count) => {
    if (count === 1) return "Avanzar 1 p\xE1gina";
    return `Avanzar ${count} p\xE1ginas`;
  },
  lastPage: "\xDAltima p\xE1gina",
  loading: "Cargando",
  locked: "Bloqueado",
  minute: "Minuto",
  month: "Mes",
  moreOptions: "M\xE1s opciones",
  mute: "Silenciar",
  nextDecade: "D\xE9cada siguiente",
  nextMonth: "Mes siguiente",
  nextPage: "P\xE1gina siguiente",
  nextSlide: "Siguiente diapositiva",
  nextVideo: "Siguiente v\xEDdeo",
  nextYear: "A\xF1o siguiente",
  noData: "No hay datos",
  noOptions: "No hay opciones",
  noResults: "No hay resultados coincidentes",
  notCompleted: "No completado",
  now: "Ahora",
  numCharacters: (num) => {
    if (num === 1) return "1 car\xE1cter";
    return `${num} caracteres`;
  },
  numCharactersRemaining: (num) => {
    if (num === 1) return "1 car\xE1cter restante";
    return `${num} caracteres restantes`;
  },
  numOptionsAvailable: (num) => {
    if (num === 0) return "No hay opciones disponibles";
    if (num === 1) return "1 opci\xF3n disponible";
    return `${num} opciones disponibles`;
  },
  numOptionsSelected: (num) => {
    if (num === 0) return "No hay opciones seleccionadas";
    if (num === 1) return "1 opci\xF3n seleccionada";
    return `${num} opci\xF3n seleccionada`;
  },
  numRowsCopied: (num) => num === 1 ? "1 fila copiada" : `${num} filas copiadas`,
  numRowsSelected: (num) => num === 1 ? "1 fila seleccionada" : `${num} filas seleccionadas`,
  optionPosition: (label, position, total) => `${label}, ${position} de ${total}`,
  optionsLoadError: "No se pudieron cargar las opciones",
  pageXOfY: (page, total) => `P\xE1gina ${page} de ${total}`,
  pagination: "Paginaci\xF3n",
  pause: "Pausar",
  pauseAnimation: "Pausar animaci\xF3n",
  pictureInPicture: "Imagen en imagen",
  pinLeft: "Fijar a la izquierda",
  pinRight: "Fijar a la derecha",
  play: "Reproducir",
  playAnimation: "Reproducir animaci\xF3n",
  playbackSpeed: "Velocidad de reproducci\xF3n",
  playlist: "Lista de reproducci\xF3n",
  pm: "PM",
  previousDecade: "D\xE9cada anterior",
  previousMonth: "Mes anterior",
  previousPage: "P\xE1gina anterior",
  previousSlide: "Diapositiva anterior",
  previousVideo: "V\xEDdeo anterior",
  previousYear: "A\xF1o anterior",
  progress: "Progreso",
  rangeTooLong: (max) => {
    if (max === 1) return "Seleccione un intervalo no mayor de 1 d\xEDa";
    return `Seleccione un intervalo no mayor de ${max} d\xEDas`;
  },
  rangeTooShort: (min) => {
    if (min === 1) return "Seleccione un intervalo de al menos 1 d\xEDa";
    return `Seleccione un intervalo de al menos ${min} d\xEDas`;
  },
  readonly: "Solo lectura",
  remove: "Eliminar",
  resetColumns: "Restablecer columnas",
  resize: "Cambiar el tama\xF1o",
  resizeColumn: "Cambiar el tama\xF1o de la columna",
  rowsPerPage: "Filas por p\xE1gina",
  scrollableRegion: "Regi\xF3n desplazable",
  scrollToEnd: "Desplazarse hasta el final",
  scrollToStart: "Desplazarse al inicio",
  search: "Buscar",
  second: "Segundo",
  seek: "Buscar",
  seekProgress: (current, duration) => `${current} de ${duration}`,
  selectAColorFromTheScreen: "Seleccione un color de la pantalla",
  selectAllRows: "Seleccionar todas las filas",
  selected: "Seleccionado",
  selectedDateLabel: (date) => `Seleccionado: ${date}`,
  selectedRangeLabel: (range) => `Intervalo seleccionado: ${range}`,
  selectGroup: "Seleccionar grupo",
  selectionCleared: "Selecci\xF3n borrada",
  selectRow: "Seleccionar fila",
  showingNofMRows: (shown, total) => `Mostrando ${shown} de ${total} filas`,
  showingXtoYofZ: (start, end, total) => `${start}\u2013${end} de ${total}`,
  showPassword: "Mostrar contrase\xF1a",
  slideNum: (slide) => `Diapositiva ${slide}`,
  sortAscending: "Ordenar de forma ascendente",
  sortColumn: "Ordenar columna",
  sortDescending: "Ordenar de forma descendente",
  startDate: "Fecha de inicio",
  steps: "Pasos",
  stepXOfY: (step, total) => `Paso ${step} de ${total}`,
  tagAdded: (tag) => `Se a\xF1adi\xF3 ${tag}`,
  tagAlreadyAdded: (tag) => `${tag} ya existe`,
  tagInputKeyboardHelp: "Pulse Retroceso o Suprimir para eliminar esta etiqueta.",
  tagRemoved: (tag) => `Se elimin\xF3 ${tag}`,
  time: "Hora",
  timeInputKeyboardHelp: "Use las teclas de flecha para cambiar los valores; presione Alt+Flecha abajo para abrir el selector de hora.",
  today: "Hoy",
  toggleColorFormat: "Alternar formato de color",
  tooFewTags: (min) => min === 1 ? "A\xF1ada al menos 1 etiqueta" : `A\xF1ada al menos ${min} etiquetas`,
  tooManyTags: (max) => max === 1 ? "A\xF1ada como m\xE1ximo 1 etiqueta" : `A\xF1ada como m\xE1ximo ${max} etiquetas`,
  unmute: "Activar sonido",
  unpin: "Desfijar",
  unpinColumn: "Desfijar columna",
  videoPlayer: "Reproductor de v\xEDdeo",
  volume: "Volumen",
  year: "A\xF1o",
  zoomIn: "Acercar",
  zoomOut: "Alejar"
};
registerTranslation(translation);
var es_default = translation;
export {
  es_default as default
};
