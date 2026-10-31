/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  qr_code_styles_default
} from "./chunk.6ZVWWDQT.js";
import {
  o
} from "./chunk.BQNDCXAL.js";
import {
  WebAwesomeElement,
  e,
  n,
  t
} from "./chunk.LBLI4KS5.js";
import {
  x
} from "./chunk.BKE5EYM3.js";
import {
  __decorateClass
} from "./chunk.JHZRD2LV.js";

// ../../node_modules/@konnorr/qr-creator/src/qr-creator.js
var qrCodeGenerator = function(_options, _element, _callback) {
};
var QrCreator = class {
  /**
   * @param {Partial<Settings>} config
   * @param {HTMLElement} $element
   * @param {() => void} [callback]
   */
  static render(config, $element, callback) {
    qrCodeGenerator(config, $element, callback);
  }
};
(function(vendor_qrcode) {
  function createQRCode(text, level, version, quiet) {
    var vqr = vendor_qrcode(version, level);
    vqr.addData(text);
    vqr.make();
    quiet = quiet || 0;
    var qrModuleCount = vqr.getModuleCount();
    var quietModuleCount = vqr.getModuleCount() + 2 * quiet;
    function isDark(row, col) {
      row -= quiet;
      col -= quiet;
      if (row < 0 || row >= qrModuleCount || col < 0 || col >= qrModuleCount) {
        return false;
      }
      return vqr.isDark(row, col);
    }
    return {
      text,
      level,
      version,
      moduleCount: quietModuleCount,
      isDark
    };
  }
  function createMinQRCode(text, level, minVersion, maxVersion, quiet) {
    minVersion = /** @type {VERSION_NUM} */
    Math.max(1, minVersion || 1);
    maxVersion = /** @type {VERSION_NUM} */
    Math.min(40, maxVersion || 40);
    for (var version = minVersion; version <= maxVersion; version += 1) {
      try {
        return createQRCode(text, level, version, quiet);
      } catch (err) {
      }
    }
    return void 0;
  }
  function drawBackground(_qr, context, settings) {
    if (settings.background) {
      context.fillStyle = settings.background;
      context.fillRect(settings.left, settings.top, settings.size, settings.size);
    }
  }
  function drawModuleRoundedDark(ctx, l, t2, r, b, rad, nw, ne, se, sw) {
    if (nw) {
      ctx.moveTo(l + rad, t2);
    } else {
      ctx.moveTo(l, t2);
    }
    function lal(b2, x0, y0, x1, y1, r0, r1) {
      if (b2) {
        ctx.lineTo(x0 + r0, y0 + r1);
        ctx.arcTo(x0, y0, x1, y1, rad);
      } else {
        ctx.lineTo(x0, y0);
      }
    }
    lal(ne, r, t2, r, b, -rad, 0);
    lal(se, r, b, l, b, 0, -rad);
    lal(sw, l, b, l, t2, rad, 0);
    lal(nw, l, t2, r, t2, 0, rad);
  }
  function drawModuleRoundendLight(ctx, l, t2, r, b, rad, nw, ne, se, sw) {
    function mlla(x2, y, r0, r1) {
      ctx.moveTo(x2 + r0, y);
      ctx.lineTo(x2, y);
      ctx.lineTo(x2, y + r1);
      ctx.arcTo(x2, y, x2 + r0, y, rad);
    }
    if (nw) mlla(l, t2, rad, rad);
    if (ne) mlla(r, t2, -rad, rad);
    if (se) mlla(r, b, -rad, -rad);
    if (sw) mlla(l, b, rad, -rad);
  }
  function drawModuleRounded(qr, context, settings, left, top, width, row, col) {
    var isDark = qr.isDark, right = left + width, bottom = top + width, rowT = row - 1, rowB = row + 1, colL = col - 1, colR = col + 1, radius = Math.floor(Math.min(0.5, Math.max(0, settings.radius)) * width), center = isDark(row, col), northwest = isDark(rowT, colL), north = isDark(rowT, col), northeast = isDark(rowT, colR), east = isDark(row, colR), southeast = isDark(rowB, colR), south = isDark(rowB, col), southwest = isDark(rowB, colL), west = isDark(row, colL);
    left = Math.round(left);
    top = Math.round(top);
    right = Math.round(right);
    bottom = Math.round(bottom);
    if (center) {
      drawModuleRoundedDark(context, left, top, right, bottom, radius, !north && !west, !north && !east, !south && !east, !south && !west);
    } else {
      drawModuleRoundendLight(context, left, top, right, bottom, radius, north && west && northwest, north && east && northeast, south && east && southeast, south && west && southwest);
    }
  }
  function drawModules(qr, context, settings, corners) {
    var moduleCount = qr.moduleCount;
    var moduleSize = settings.size / moduleCount;
    var row = 0;
    var col = 0;
    context.beginPath();
    const cornerOffset = 7 + settings.quiet;
    for (row = 0; row < moduleCount; row += 1) {
      for (col = 0; col < moduleCount; col += 1) {
        const isCorner = col < cornerOffset && row < cornerOffset || col >= moduleCount - cornerOffset && row < cornerOffset || col < cornerOffset && row >= moduleCount - cornerOffset;
        if (isCorner !== corners) {
          continue;
        }
        var l = settings.left + col * moduleSize;
        var t2 = settings.top + row * moduleSize;
        var w = moduleSize;
        drawModuleRounded(qr, context, settings, l, t2, w, row, col);
      }
    }
    setFill(context, settings, corners);
    context.fill();
  }
  function setFill(context, settings, corners) {
    const fill = corners ? settings.cornerFill || settings.fill : settings.fill;
    if (typeof fill === "string") {
      context.fillStyle = fill;
      return;
    }
    const type = fill["type"];
    const position = fill["position"];
    const colorStops = fill["colorStops"];
    let gradient;
    if (type === "linear-gradient") {
      const absolutePosition = (
        /** @type {Parameters<CanvasRenderingContext2D["createLinearGradient"]>} */
        position.slice(0, 4).map((coordinate) => Math.round(coordinate * settings.size))
      );
      gradient = context.createLinearGradient.apply(context, absolutePosition);
    } else if (type === "radial-gradient") {
      const absolutePosition = (
        /** @type {Parameters<CanvasRenderingContext2D["createRadialGradient"]>} */
        position.slice(0, 6).map((coordinate) => Math.round(coordinate * settings.size))
      );
      gradient = context.createRadialGradient.apply(context, absolutePosition);
    } else {
      throw new Error("Unsupported fill");
    }
    colorStops.forEach(([offset, color]) => {
      gradient.addColorStop(offset, color);
    });
    context.fillStyle = gradient;
  }
  function drawOnCanvas(qr, canvas, settings) {
    qr = createMinQRCode(settings.text, settings.ecLevel, settings.minVersion, settings.maxVersion, settings.quiet);
    if (!qr) {
      return null;
    }
    var context = canvas.getContext("2d");
    if (!context) {
      return canvas;
    }
    drawBackground(qr, context, settings);
    drawModules(qr, context, settings, true);
    drawModules(qr, context, settings, false);
    return canvas;
  }
  function createCanvas(qr, settings) {
    var $canvas = document.createElement("canvas");
    $canvas.width = settings.size;
    $canvas.height = settings.size;
    return drawOnCanvas(qr, $canvas, settings);
  }
  var defaults = (
    /** @type {Settings} */
    {
      // version range somewhere in 1 .. 40
      "minVersion": 1,
      "maxVersion": 40,
      // error correction level: `'L'`, `'M'`, `'Q'` or `'H'`
      "ecLevel": "L",
      // offset in pixel if drawn onto existing canvas
      "left": 0,
      "top": 0,
      // size in pixel
      "size": 200,
      // code color or image element
      "fill": "#000",
      "cornerFill": null,
      // Falls back to fill
      // background color, `null` for transparent background
      "background": null,
      // content
      "text": "no text",
      // corner radius relative to module width: 0.0 .. 0.5
      "radius": 0.5,
      // quiet zone in modules
      "quiet": 0,
      "image": null,
      "imageEcCover": 0.5
    }
  );
  qrCodeGenerator = function(options, $element, callback) {
    var settings = Object.assign({}, defaults, options);
    settings.minVersion = settings["minVersion"];
    settings.maxVersion = settings["maxVersion"];
    settings.ecLevel = settings["ecLevel"];
    settings.left = settings["left"];
    settings.top = settings["top"];
    settings.size = settings["size"];
    settings.fill = settings["fill"];
    settings.background = settings["background"];
    settings.text = settings["text"];
    settings.radius = settings["radius"];
    settings.quiet = settings["quiet"];
    settings.cornerFill = settings["cornerFill"] || settings.fill;
    settings.image = settings["image"];
    settings.imageBackground = settings["imageBackground"];
    settings.imageEcCover = settings["imageEcCover"];
    settings.imagePadding = settings["imagePadding"];
    var qr = createMinQRCode(
      settings.text,
      settings.ecLevel,
      settings.minVersion,
      settings.maxVersion,
      settings.quiet
    );
    if (!qr) {
      return;
    }
    callback = callback || function() {
    };
    const render = function() {
      var canvasElement = $element;
      if ($element instanceof HTMLCanvasElement) {
        if ($element.width !== settings.size || $element.height !== settings.size) {
          $element.width = settings.size;
          $element.height = settings.size;
        }
        const context = $element.getContext("2d");
        if (context) {
          context.clearRect(0, 0, $element.width, $element.height);
        }
        drawOnCanvas(
          qr,
          $element,
          /** @type {Settings} */
          settings
        );
      } else {
        if (qr) {
          const canvasEl = createCanvas(
            qr,
            /** @type {Settings} */
            settings
          );
          if (canvasEl) {
            canvasElement = canvasEl;
            $element.appendChild(canvasElement);
          }
        }
      }
      return (
        /** @type {HTMLCanvasElement} */
        canvasElement
      );
    };
    if (settings.image) {
      const img = new Image();
      img.onload = function() {
        if (!qr) {
          return;
        }
        const ecCover = (
          /** @type {number} */
          settings.imageEcCover ?? defaults.imageEcCover
        );
        const quietModuleCount = qr.moduleCount - settings.quiet * 2;
        const moduleSize = settings.size / quietModuleCount;
        const ratio = img.naturalWidth / img.naturalHeight;
        let maxImageWidth = settings.size * ecCover;
        maxImageWidth = Math.min(maxImageWidth, maxImageWidth * ratio);
        let maxImageHeight = settings.size * ecCover;
        maxImageHeight = Math.min(maxImageHeight, maxImageHeight / ratio);
        const dataPixels = quietModuleCount * quietModuleCount - (49 * 3 + 25);
        const area = {
          "L": 0.07,
          "M": 0.15,
          "Q": 0.25,
          "H": 0.3
        }[settings.ecLevel] * ecCover * dataPixels | 0;
        var imageModuleWidth = Math.min(quietModuleCount, Math.sqrt(area * ratio) | 0, maxImageWidth);
        var imageModuleHeight = imageModuleWidth / ratio | 0;
        if (imageModuleHeight > quietModuleCount) {
          imageModuleHeight = quietModuleCount;
          imageModuleWidth = imageModuleHeight * ratio | 0;
        }
        imageModuleHeight = Math.min(imageModuleHeight, maxImageHeight);
        const imageModuleLeft = qr.moduleCount / 2 - imageModuleWidth / 2 | 0;
        const imageModuleTop = qr.moduleCount / 2 - imageModuleHeight / 2 | 0;
        const isDark = qr.isDark;
        qr.isDark = function(row, col) {
          if (imageModuleLeft <= col && col < imageModuleLeft + imageModuleWidth && imageModuleTop <= row && row < imageModuleTop + imageModuleHeight) {
            return false;
          }
          return isDark(row, col);
        };
        const imageFloatModuleWidth = Math.min(imageModuleWidth, imageModuleHeight * ratio) - settings.quiet;
        const imageFloatModuleHeight = Math.min(imageModuleHeight, imageModuleWidth / ratio) - settings.quiet;
        const imageLeft = imageModuleLeft + (imageModuleWidth - imageFloatModuleWidth) / 2 - settings.quiet;
        const imageTop = imageModuleTop + (imageModuleHeight - imageFloatModuleHeight) / 2 - settings.quiet;
        let left = imageLeft * moduleSize;
        let top = imageTop * moduleSize;
        let width = imageFloatModuleWidth * moduleSize;
        let height = imageFloatModuleHeight * moduleSize;
        var canvas = render();
        const context = canvas.getContext("2d");
        if (context) {
          context.fillStyle = settings.imageBackground || "transparent";
          context.fillRect(
            left - 4,
            top - 4,
            width + 8,
            height + 8
          );
          context.drawImage(
            img,
            left,
            top,
            width,
            height
          );
        }
        callback();
      };
      img.onerror = () => {
        render();
        callback();
      };
      img.src = settings.image;
    } else {
      render();
      callback();
    }
  };
})((function() {
  var qrcode = (function() {
    function qrcode2(typeNumber, errorCorrectLevel) {
      var PAD0 = 236;
      var PAD1 = 17;
      var _typeNumber = typeNumber;
      var _errorCorrectLevel = QRErrorCorrectLevel[errorCorrectLevel];
      var _modules = null;
      var _moduleCount = 0;
      var _dataCache = null;
      var _dataList = new Array();
      var _this = {};
      var makeImpl = function(test, maskPattern) {
        _moduleCount = _typeNumber * 4 + 17;
        _modules = (function(moduleCount) {
          var modules = new Array(moduleCount);
          for (var row = 0; row < moduleCount; row += 1) {
            modules[row] = new Array(moduleCount);
            for (var col = 0; col < moduleCount; col += 1) {
              modules[row][col] = null;
            }
          }
          return modules;
        })(_moduleCount);
        setupPositionProbePattern(0, 0);
        setupPositionProbePattern(_moduleCount - 7, 0);
        setupPositionProbePattern(0, _moduleCount - 7);
        setupPositionAdjustPattern();
        setupTimingPattern();
        setupTypeInfo(test, maskPattern);
        if (_typeNumber >= 7) {
          setupTypeNumber(test);
        }
        if (_dataCache == null) {
          _dataCache = createData(_typeNumber, _errorCorrectLevel, _dataList);
        }
        mapData(_dataCache, maskPattern);
      };
      var setupPositionProbePattern = function(row, col) {
        if (_modules == null) {
          return;
        }
        for (var r = -1; r <= 7; r += 1) {
          if (row + r <= -1 || _moduleCount <= row + r) continue;
          for (var c = -1; c <= 7; c += 1) {
            if (col + c <= -1 || _moduleCount <= col + c) continue;
            if (0 <= r && r <= 6 && (c == 0 || c == 6) || 0 <= c && c <= 6 && (r == 0 || r == 6) || 2 <= r && r <= 4 && 2 <= c && c <= 4) {
              _modules[row + r][col + c] = true;
            } else {
              _modules[row + r][col + c] = false;
            }
          }
        }
      };
      var getBestMaskPattern = function() {
        var minLostPoint = 0, pattern = 0;
        for (var i = 0; i < 8; i += 1) {
          makeImpl(true, i);
          var lostPoint = QRUtil.getLostPoint(_this);
          if (i == 0 || minLostPoint > lostPoint) {
            minLostPoint = lostPoint;
            pattern = i;
          }
        }
        return pattern;
      };
      var setupTimingPattern = function() {
        if (!_modules) {
          return;
        }
        for (var r = 8; r < _moduleCount - 8; r += 1) {
          if (_modules[r][6] != null) {
            continue;
          }
          _modules[r][6] = r % 2 == 0;
        }
        for (var c = 8; c < _moduleCount - 8; c += 1) {
          if (_modules[6][c] != null) {
            continue;
          }
          _modules[6][c] = c % 2 == 0;
        }
      };
      var setupPositionAdjustPattern = function() {
        if (!_modules) {
          return;
        }
        var pos = QRUtil.getPatternPosition(_typeNumber);
        for (var i = 0; i < pos.length; i += 1) {
          for (var j = 0; j < pos.length; j += 1) {
            var row = pos[i];
            var col = pos[j];
            if (_modules[row][col] != null) {
              continue;
            }
            for (var r = -2; r <= 2; r += 1) {
              for (var c = -2; c <= 2; c += 1) {
                _modules[row + r][col + c] = r == -2 || r == 2 || c == -2 || c == 2 || r == 0 && c == 0;
              }
            }
          }
        }
      };
      var setupTypeNumber = function(test) {
        if (!_modules) {
          return;
        }
        var bits = QRUtil.getBCHTypeNumber(_typeNumber);
        for (var i = 0; i < 18; i += 1) {
          var mod = !test && (bits >> i & 1) == 1;
          _modules[Math.floor(i / 3)][i % 3 + _moduleCount - 8 - 3] = mod;
        }
        for (var i = 0; i < 18; i += 1) {
          var mod = !test && (bits >> i & 1) == 1;
          _modules[i % 3 + _moduleCount - 8 - 3][Math.floor(i / 3)] = mod;
        }
      };
      var setupTypeInfo = function(test, maskPattern) {
        var data = _errorCorrectLevel << 3 | maskPattern;
        var bits = QRUtil.getBCHTypeInfo(data);
        if (!_modules) {
          return;
        }
        for (var i = 0; i < 15; i += 1) {
          let mod = !test && (bits >> i & 1) == 1;
          _modules[i < 6 ? i : i < 8 ? i + 1 : _moduleCount - 15 + i][8] = mod;
          _modules[8][i < 8 ? _moduleCount - i - 1 : i < 9 ? 15 - i : 14 - i] = mod;
        }
        _modules[_moduleCount - 8][8] = !test;
      };
      var mapData = function(data, maskPattern) {
        var inc = -1, row = _moduleCount - 1, bitIndex = 7, byteIndex = 0, maskFunc = QRUtil.getMaskFunction(maskPattern);
        for (var col = _moduleCount - 1; col > 0; col -= 2) {
          if (col == 6) col -= 1;
          while (true) {
            for (var c = 0; c < 2; c += 1) {
              if (_modules && _modules[row][col - c] == null) {
                var dark = false;
                if (byteIndex < data.length) {
                  dark = (data[byteIndex] >>> bitIndex & 1) == 1;
                }
                var mask = maskFunc(row, col - c);
                if (mask) {
                  dark = !dark;
                }
                _modules[row][col - c] = dark;
                bitIndex -= 1;
                if (bitIndex == -1) {
                  byteIndex += 1;
                  bitIndex = 7;
                }
              }
            }
            row += inc;
            if (row < 0 || _moduleCount <= row) {
              row -= inc;
              inc = -inc;
              break;
            }
          }
        }
      };
      var createBytes = function(buffer, rsBlocks) {
        var offset = 0, maxDcCount = 0, maxEcCount = 0, dcdata = new Array(rsBlocks.length), ecdata = new Array(rsBlocks.length);
        for (var r = 0; r < rsBlocks.length; r += 1) {
          var dcCount = rsBlocks[r].dataCount, ecCount = rsBlocks[r].totalCount - dcCount;
          maxDcCount = Math.max(maxDcCount, dcCount);
          maxEcCount = Math.max(maxEcCount, ecCount);
          dcdata[r] = new Array(dcCount);
          for (var i = 0; i < dcdata[r].length; i += 1) {
            dcdata[r][i] = 255 & buffer.getBuffer()[i + offset];
          }
          offset += dcCount;
          var rsPoly = QRUtil.getErrorCorrectPolynomial(ecCount), rawPoly = qrPolynomial(dcdata[r], rsPoly.getLength() - 1), modPoly = rawPoly.mod(rsPoly);
          ecdata[r] = new Array(rsPoly.getLength() - 1);
          for (var i = 0; i < ecdata[r].length; i += 1) {
            var modIndex = i + modPoly.getLength() - ecdata[r].length;
            ecdata[r][i] = modIndex >= 0 ? modPoly.getAt(modIndex) : 0;
          }
        }
        var totalCodeCount = 0;
        for (var i = 0; i < rsBlocks.length; i += 1) {
          totalCodeCount += rsBlocks[i].totalCount;
        }
        var data = new Array(totalCodeCount);
        var index = 0;
        for (var i = 0; i < maxDcCount; i += 1) {
          for (var r = 0; r < rsBlocks.length; r += 1) {
            if (i < dcdata[r].length) {
              data[index] = dcdata[r][i];
              index += 1;
            }
          }
        }
        for (var i = 0; i < maxEcCount; i += 1) {
          for (var r = 0; r < rsBlocks.length; r += 1) {
            if (i < ecdata[r].length) {
              data[index] = ecdata[r][i];
              index += 1;
            }
          }
        }
        return data;
      };
      var createData = function(typeNumber2, errorCorrectLevel2, dataList) {
        var rsBlocks = QRRSBlock.getRSBlocks(typeNumber2, errorCorrectLevel2);
        var buffer = qrBitBuffer();
        for (var i = 0; i < dataList.length; i += 1) {
          var data = dataList[i];
          buffer.put(data.getMode(), 4);
          buffer.put(data.getLength(), QRUtil.getLengthInBits(data.getMode(), typeNumber2));
          data.write(buffer);
        }
        var totalDataCount = 0;
        for (var i = 0; i < rsBlocks.length; i += 1) {
          totalDataCount += rsBlocks[i].dataCount;
        }
        if (buffer.getLengthInBits() > totalDataCount * 8) {
          throw new Error("code length overflow. (" + buffer.getLengthInBits() + ">" + totalDataCount * 8 + ")");
        }
        if (buffer.getLengthInBits() + 4 <= totalDataCount * 8) {
          buffer.put(0, 4);
        }
        while (buffer.getLengthInBits() % 8 != 0) {
          buffer.putBit(false);
        }
        while (true) {
          if (buffer.getLengthInBits() >= totalDataCount * 8) {
            break;
          }
          buffer.put(PAD0, 8);
          if (buffer.getLengthInBits() >= totalDataCount * 8) {
            break;
          }
          buffer.put(PAD1, 8);
        }
        return createBytes(buffer, rsBlocks);
      };
      _this.addData = function(data) {
        var newData = qr8BitByte(data);
        _dataList.push(newData);
        _dataCache = null;
      };
      _this.isDark = function(row, col) {
        if (!_modules) {
          throw new Error("_modules is null");
        }
        if (row < 0 || _moduleCount <= row || col < 0 || _moduleCount <= col) {
          throw new Error(row + "," + col);
        }
        return _modules[row][col];
      };
      _this.getModuleCount = function() {
        return _moduleCount;
      };
      _this.make = function() {
        makeImpl(false, getBestMaskPattern());
      };
      return _this;
    }
    ;
    qrcode2.stringToBytes = function(s) {
      return new TextEncoder().encode(s);
    };
    var QRMode = {
      MODE_8BIT_BYTE: 1 << 2
    };
    var QRErrorCorrectLevel = {
      "L": 1,
      "M": 0,
      "Q": 3,
      "H": 2
    };
    var QRMaskPattern = {
      PATTERN000: 0,
      PATTERN001: 1,
      PATTERN010: 2,
      PATTERN011: 3,
      PATTERN100: 4,
      PATTERN101: 5,
      PATTERN110: 6,
      PATTERN111: 7
    };
    var QRUtil = (function() {
      var PATTERN_POSITION_TABLE = [
        [],
        [6, 18],
        [6, 22],
        [6, 26],
        [6, 30],
        [6, 34],
        [6, 22, 38],
        [6, 24, 42],
        [6, 26, 46],
        [6, 28, 50],
        [6, 30, 54],
        [6, 32, 58],
        [6, 34, 62],
        [6, 26, 46, 66],
        [6, 26, 48, 70],
        [6, 26, 50, 74],
        [6, 30, 54, 78],
        [6, 30, 56, 82],
        [6, 30, 58, 86],
        [6, 34, 62, 90],
        [6, 28, 50, 72, 94],
        [6, 26, 50, 74, 98],
        [6, 30, 54, 78, 102],
        [6, 28, 54, 80, 106],
        [6, 32, 58, 84, 110],
        [6, 30, 58, 86, 114],
        [6, 34, 62, 90, 118],
        [6, 26, 50, 74, 98, 122],
        [6, 30, 54, 78, 102, 126],
        [6, 26, 52, 78, 104, 130],
        [6, 30, 56, 82, 108, 134],
        [6, 34, 60, 86, 112, 138],
        [6, 30, 58, 86, 114, 142],
        [6, 34, 62, 90, 118, 146],
        [6, 30, 54, 78, 102, 126, 150],
        [6, 24, 50, 76, 102, 128, 154],
        [6, 28, 54, 80, 106, 132, 158],
        [6, 32, 58, 84, 110, 136, 162],
        [6, 26, 54, 82, 110, 138, 166],
        [6, 30, 58, 86, 114, 142, 170]
      ];
      var G15 = 1 << 10 | 1 << 8 | 1 << 5 | 1 << 4 | 1 << 2 | 1 << 1 | 1 << 0;
      var G18 = 1 << 12 | 1 << 11 | 1 << 10 | 1 << 9 | 1 << 8 | 1 << 5 | 1 << 2 | 1 << 0;
      var G15_MASK = 1 << 14 | 1 << 12 | 1 << 10 | 1 << 4 | 1 << 1;
      var _this = {};
      var getBCHDigit = function(data) {
        var digit = 0;
        while (data != 0) {
          digit += 1;
          data >>>= 1;
        }
        return digit;
      };
      _this.getBCHTypeInfo = function(data) {
        var d = data << 10;
        while (getBCHDigit(d) - getBCHDigit(G15) >= 0) {
          d ^= G15 << getBCHDigit(d) - getBCHDigit(G15);
        }
        return (data << 10 | d) ^ G15_MASK;
      };
      _this.getBCHTypeNumber = function(data) {
        var d = data << 12;
        while (getBCHDigit(d) - getBCHDigit(G18) >= 0) {
          d ^= G18 << getBCHDigit(d) - getBCHDigit(G18);
        }
        return data << 12 | d;
      };
      _this.getPatternPosition = function(typeNumber) {
        return PATTERN_POSITION_TABLE[typeNumber - 1];
      };
      _this.getMaskFunction = function(maskPattern) {
        switch (maskPattern) {
          case QRMaskPattern.PATTERN000:
            return function(i, j) {
              return (i + j) % 2 == 0;
            };
          case QRMaskPattern.PATTERN001:
            return function(i, _j) {
              return i % 2 == 0;
            };
          case QRMaskPattern.PATTERN010:
            return function(_i, j) {
              return j % 3 == 0;
            };
          case QRMaskPattern.PATTERN011:
            return function(i, j) {
              return (i + j) % 3 == 0;
            };
          case QRMaskPattern.PATTERN100:
            return function(i, j) {
              return (Math.floor(i / 2) + Math.floor(j / 3)) % 2 == 0;
            };
          case QRMaskPattern.PATTERN101:
            return function(i, j) {
              return i * j % 2 + i * j % 3 == 0;
            };
          case QRMaskPattern.PATTERN110:
            return function(i, j) {
              return (i * j % 2 + i * j % 3) % 2 == 0;
            };
          case QRMaskPattern.PATTERN111:
            return function(i, j) {
              return (i * j % 3 + (i + j) % 2) % 2 == 0;
            };
          default:
            throw new Error("bad maskPattern:" + maskPattern);
        }
      };
      _this.getErrorCorrectPolynomial = function(errorCorrectLength) {
        var a = qrPolynomial([1], 0);
        for (var i = 0; i < errorCorrectLength; i += 1) {
          a = a.multiply(qrPolynomial([1, QRMath.gexp(i)], 0));
        }
        return a;
      };
      _this.getLengthInBits = function(mode, type) {
        if (mode != QRMode.MODE_8BIT_BYTE || type < 1 || type > 40)
          throw new Error("mode: " + mode + "; type: " + type);
        return type < 10 ? 8 : 16;
      };
      _this.getLostPoint = function(qrcode3) {
        var moduleCount = qrcode3.getModuleCount();
        var lostPoint = 0;
        for (var row = 0; row < moduleCount; row += 1) {
          for (var col = 0; col < moduleCount; col += 1) {
            var sameCount = 0, dark = qrcode3.isDark(row, col);
            for (var r = -1; r <= 1; r += 1) {
              if (row + r < 0 || moduleCount <= row + r) {
                continue;
              }
              for (var c = -1; c <= 1; c += 1) {
                if (col + c < 0 || moduleCount <= col + c) {
                  continue;
                }
                if (r == 0 && c == 0) {
                  continue;
                }
                if (dark == qrcode3.isDark(row + r, col + c)) {
                  sameCount += 1;
                }
              }
            }
            if (sameCount > 5) {
              lostPoint += 3 + sameCount - 5;
            }
          }
        }
        ;
        for (var row = 0; row < moduleCount - 1; row += 1) {
          for (var col = 0; col < moduleCount - 1; col += 1) {
            var count = 0;
            if (qrcode3.isDark(row, col)) count += 1;
            if (qrcode3.isDark(row + 1, col)) count += 1;
            if (qrcode3.isDark(row, col + 1)) count += 1;
            if (qrcode3.isDark(row + 1, col + 1)) count += 1;
            if (count == 0 || count == 4) {
              lostPoint += 3;
            }
          }
        }
        for (var row = 0; row < moduleCount; row += 1) {
          for (var col = 0; col < moduleCount - 6; col += 1) {
            if (qrcode3.isDark(row, col) && !qrcode3.isDark(row, col + 1) && qrcode3.isDark(row, col + 2) && qrcode3.isDark(row, col + 3) && qrcode3.isDark(row, col + 4) && !qrcode3.isDark(row, col + 5) && qrcode3.isDark(row, col + 6)) {
              lostPoint += 40;
            }
          }
        }
        for (var col = 0; col < moduleCount; col += 1) {
          for (var row = 0; row < moduleCount - 6; row += 1) {
            if (qrcode3.isDark(row, col) && !qrcode3.isDark(row + 1, col) && qrcode3.isDark(row + 2, col) && qrcode3.isDark(row + 3, col) && qrcode3.isDark(row + 4, col) && !qrcode3.isDark(row + 5, col) && qrcode3.isDark(row + 6, col)) {
              lostPoint += 40;
            }
          }
        }
        var darkCount = 0;
        for (var col = 0; col < moduleCount; col += 1) {
          for (var row = 0; row < moduleCount; row += 1) {
            if (qrcode3.isDark(row, col)) {
              darkCount += 1;
            }
          }
        }
        var ratio = Math.abs(100 * darkCount / moduleCount / moduleCount - 50) / 5;
        lostPoint += ratio * 10;
        return lostPoint;
      };
      return _this;
    })();
    var QRMath = (function() {
      var LOG_TABLE = new Array(256);
      var EXP_TABLE = new Array(256);
      for (var i = 0; i < 8; i += 1) {
        EXP_TABLE[i] = 1 << i;
      }
      for (var i = 8; i < 256; i += 1) {
        EXP_TABLE[i] = EXP_TABLE[i - 4] ^ EXP_TABLE[i - 5] ^ EXP_TABLE[i - 6] ^ EXP_TABLE[i - 8];
      }
      for (var i = 0; i < 255; i += 1) {
        LOG_TABLE[EXP_TABLE[i]] = i;
      }
      var _this = {};
      _this.glog = function(n2) {
        if (n2 < 1) {
          throw new Error("glog(" + n2 + ")");
        }
        return LOG_TABLE[n2];
      };
      _this.gexp = function(n2) {
        while (n2 < 0) {
          n2 += 255;
        }
        while (n2 >= 256) {
          n2 -= 255;
        }
        return EXP_TABLE[n2];
      };
      return _this;
    })();
    function qrPolynomial(num, shift) {
      if (typeof num.length == "undefined") {
        throw new Error(num.length + "/" + shift);
      }
      var _num = (function() {
        var offset = 0;
        while (offset < num.length && num[offset] == 0) {
          offset += 1;
        }
        var _num2 = new Array(num.length - offset + shift);
        for (var i = 0; i < num.length - offset; i += 1) {
          _num2[i] = num[i + offset];
        }
        return _num2;
      })();
      var _this = {};
      _this.getAt = function(index) {
        return _num[index];
      };
      _this.getLength = function() {
        return _num.length;
      };
      _this.multiply = function(e2) {
        var num2 = new Array(_this.getLength() + e2.getLength() - 1);
        for (var i = 0; i < _this.getLength(); i += 1) {
          for (var j = 0; j < e2.getLength(); j += 1) {
            num2[i + j] ^= QRMath.gexp(QRMath.glog(_this.getAt(i)) + QRMath.glog(e2.getAt(j)));
          }
        }
        return qrPolynomial(num2, 0);
      };
      _this.mod = function(e2) {
        if (_this.getLength() - e2.getLength() < 0) {
          return _this;
        }
        var ratio = QRMath.glog(_this.getAt(0)) - QRMath.glog(e2.getAt(0));
        var num2 = new Array(_this.getLength());
        for (var i = 0; i < _this.getLength(); i += 1) {
          num2[i] = _this.getAt(i);
        }
        for (var i = 0; i < e2.getLength(); i += 1) {
          num2[i] ^= QRMath.gexp(QRMath.glog(e2.getAt(i)) + ratio);
        }
        return qrPolynomial(num2, 0).mod(e2);
      };
      return _this;
    }
    ;
    var QRRSBlock = (function() {
      var RS_BLOCK_TABLE = [
        // L
        // M
        // Q
        // H
        // 1
        [1, 26, 19],
        [1, 26, 16],
        [1, 26, 13],
        [1, 26, 9],
        // 2
        [1, 44, 34],
        [1, 44, 28],
        [1, 44, 22],
        [1, 44, 16],
        // 3
        [1, 70, 55],
        [1, 70, 44],
        [2, 35, 17],
        [2, 35, 13],
        // 4
        [1, 100, 80],
        [2, 50, 32],
        [2, 50, 24],
        [4, 25, 9],
        // 5
        [1, 134, 108],
        [2, 67, 43],
        [2, 33, 15, 2, 34, 16],
        [2, 33, 11, 2, 34, 12],
        // 6
        [2, 86, 68],
        [4, 43, 27],
        [4, 43, 19],
        [4, 43, 15],
        // 7
        [2, 98, 78],
        [4, 49, 31],
        [2, 32, 14, 4, 33, 15],
        [4, 39, 13, 1, 40, 14],
        // 8
        [2, 121, 97],
        [2, 60, 38, 2, 61, 39],
        [4, 40, 18, 2, 41, 19],
        [4, 40, 14, 2, 41, 15],
        // 9
        [2, 146, 116],
        [3, 58, 36, 2, 59, 37],
        [4, 36, 16, 4, 37, 17],
        [4, 36, 12, 4, 37, 13],
        // 10
        [2, 86, 68, 2, 87, 69],
        [4, 69, 43, 1, 70, 44],
        [6, 43, 19, 2, 44, 20],
        [6, 43, 15, 2, 44, 16],
        // 11
        [4, 101, 81],
        [1, 80, 50, 4, 81, 51],
        [4, 50, 22, 4, 51, 23],
        [3, 36, 12, 8, 37, 13],
        // 12
        [2, 116, 92, 2, 117, 93],
        [6, 58, 36, 2, 59, 37],
        [4, 46, 20, 6, 47, 21],
        [7, 42, 14, 4, 43, 15],
        // 13
        [4, 133, 107],
        [8, 59, 37, 1, 60, 38],
        [8, 44, 20, 4, 45, 21],
        [12, 33, 11, 4, 34, 12],
        // 14
        [3, 145, 115, 1, 146, 116],
        [4, 64, 40, 5, 65, 41],
        [11, 36, 16, 5, 37, 17],
        [11, 36, 12, 5, 37, 13],
        // 15
        [5, 109, 87, 1, 110, 88],
        [5, 65, 41, 5, 66, 42],
        [5, 54, 24, 7, 55, 25],
        [11, 36, 12, 7, 37, 13],
        // 16
        [5, 122, 98, 1, 123, 99],
        [7, 73, 45, 3, 74, 46],
        [15, 43, 19, 2, 44, 20],
        [3, 45, 15, 13, 46, 16],
        // 17
        [1, 135, 107, 5, 136, 108],
        [10, 74, 46, 1, 75, 47],
        [1, 50, 22, 15, 51, 23],
        [2, 42, 14, 17, 43, 15],
        // 18
        [5, 150, 120, 1, 151, 121],
        [9, 69, 43, 4, 70, 44],
        [17, 50, 22, 1, 51, 23],
        [2, 42, 14, 19, 43, 15],
        // 19
        [3, 141, 113, 4, 142, 114],
        [3, 70, 44, 11, 71, 45],
        [17, 47, 21, 4, 48, 22],
        [9, 39, 13, 16, 40, 14],
        // 20
        [3, 135, 107, 5, 136, 108],
        [3, 67, 41, 13, 68, 42],
        [15, 54, 24, 5, 55, 25],
        [15, 43, 15, 10, 44, 16],
        // 21
        [4, 144, 116, 4, 145, 117],
        [17, 68, 42],
        [17, 50, 22, 6, 51, 23],
        [19, 46, 16, 6, 47, 17],
        // 22
        [2, 139, 111, 7, 140, 112],
        [17, 74, 46],
        [7, 54, 24, 16, 55, 25],
        [34, 37, 13],
        // 23
        [4, 151, 121, 5, 152, 122],
        [4, 75, 47, 14, 76, 48],
        [11, 54, 24, 14, 55, 25],
        [16, 45, 15, 14, 46, 16],
        // 24
        [6, 147, 117, 4, 148, 118],
        [6, 73, 45, 14, 74, 46],
        [11, 54, 24, 16, 55, 25],
        [30, 46, 16, 2, 47, 17],
        // 25
        [8, 132, 106, 4, 133, 107],
        [8, 75, 47, 13, 76, 48],
        [7, 54, 24, 22, 55, 25],
        [22, 45, 15, 13, 46, 16],
        // 26
        [10, 142, 114, 2, 143, 115],
        [19, 74, 46, 4, 75, 47],
        [28, 50, 22, 6, 51, 23],
        [33, 46, 16, 4, 47, 17],
        // 27
        [8, 152, 122, 4, 153, 123],
        [22, 73, 45, 3, 74, 46],
        [8, 53, 23, 26, 54, 24],
        [12, 45, 15, 28, 46, 16],
        // 28
        [3, 147, 117, 10, 148, 118],
        [3, 73, 45, 23, 74, 46],
        [4, 54, 24, 31, 55, 25],
        [11, 45, 15, 31, 46, 16],
        // 29
        [7, 146, 116, 7, 147, 117],
        [21, 73, 45, 7, 74, 46],
        [1, 53, 23, 37, 54, 24],
        [19, 45, 15, 26, 46, 16],
        // 30
        [5, 145, 115, 10, 146, 116],
        [19, 75, 47, 10, 76, 48],
        [15, 54, 24, 25, 55, 25],
        [23, 45, 15, 25, 46, 16],
        // 31
        [13, 145, 115, 3, 146, 116],
        [2, 74, 46, 29, 75, 47],
        [42, 54, 24, 1, 55, 25],
        [23, 45, 15, 28, 46, 16],
        // 32
        [17, 145, 115],
        [10, 74, 46, 23, 75, 47],
        [10, 54, 24, 35, 55, 25],
        [19, 45, 15, 35, 46, 16],
        // 33
        [17, 145, 115, 1, 146, 116],
        [14, 74, 46, 21, 75, 47],
        [29, 54, 24, 19, 55, 25],
        [11, 45, 15, 46, 46, 16],
        // 34
        [13, 145, 115, 6, 146, 116],
        [14, 74, 46, 23, 75, 47],
        [44, 54, 24, 7, 55, 25],
        [59, 46, 16, 1, 47, 17],
        // 35
        [12, 151, 121, 7, 152, 122],
        [12, 75, 47, 26, 76, 48],
        [39, 54, 24, 14, 55, 25],
        [22, 45, 15, 41, 46, 16],
        // 36
        [6, 151, 121, 14, 152, 122],
        [6, 75, 47, 34, 76, 48],
        [46, 54, 24, 10, 55, 25],
        [2, 45, 15, 64, 46, 16],
        // 37
        [17, 152, 122, 4, 153, 123],
        [29, 74, 46, 14, 75, 47],
        [49, 54, 24, 10, 55, 25],
        [24, 45, 15, 46, 46, 16],
        // 38
        [4, 152, 122, 18, 153, 123],
        [13, 74, 46, 32, 75, 47],
        [48, 54, 24, 14, 55, 25],
        [42, 45, 15, 32, 46, 16],
        // 39
        [20, 147, 117, 4, 148, 118],
        [40, 75, 47, 7, 76, 48],
        [43, 54, 24, 22, 55, 25],
        [10, 45, 15, 67, 46, 16],
        // 40
        [19, 148, 118, 6, 149, 119],
        [18, 75, 47, 31, 76, 48],
        [34, 54, 24, 34, 55, 25],
        [20, 45, 15, 61, 46, 16]
      ];
      var qrRSBlock = function(totalCount, dataCount) {
        var _this2 = {};
        _this2.totalCount = totalCount;
        _this2.dataCount = dataCount;
        return _this2;
      };
      var _this = {};
      var getRsBlockTable = function(typeNumber, errorCorrectLevel) {
        switch (errorCorrectLevel) {
          case QRErrorCorrectLevel["L"]:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 0];
          case QRErrorCorrectLevel["M"]:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 1];
          case QRErrorCorrectLevel["Q"]:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 2];
          case QRErrorCorrectLevel["H"]:
            return RS_BLOCK_TABLE[(typeNumber - 1) * 4 + 3];
          default:
            return void 0;
        }
      };
      _this.getRSBlocks = function(typeNumber, errorCorrectLevel) {
        var rsBlock = getRsBlockTable(typeNumber, errorCorrectLevel);
        if (typeof rsBlock == "undefined") {
          throw new Error("bad rs block @ typeNumber:" + typeNumber + "/errorCorrectLevel:" + errorCorrectLevel);
        }
        var length = rsBlock.length / 3, list = new Array();
        for (var i = 0; i < length; i += 1) {
          var count = rsBlock[i * 3 + 0], totalCount = rsBlock[i * 3 + 1], dataCount = rsBlock[i * 3 + 2];
          for (var j = 0; j < count; j += 1) {
            list.push(qrRSBlock(totalCount, dataCount));
          }
        }
        return list;
      };
      return _this;
    })();
    var qrBitBuffer = function() {
      var _buffer = new Array();
      var _length = 0;
      var _this = {};
      _this.getBuffer = function() {
        return _buffer;
      };
      _this.getAt = function(index) {
        var bufIndex = Math.floor(index / 8);
        return (_buffer[bufIndex] >>> 7 - index % 8 & 1) == 1;
      };
      _this.put = function(num, length) {
        for (var i = 0; i < length; i += 1) {
          _this.putBit((num >>> length - i - 1 & 1) == 1);
        }
      };
      _this.getLengthInBits = function() {
        return _length;
      };
      _this.putBit = function(bit) {
        var bufIndex = Math.floor(_length / 8);
        if (_buffer.length <= bufIndex) {
          _buffer.push(0);
        }
        if (bit) {
          _buffer[bufIndex] |= 128 >>> _length % 8;
        }
        _length += 1;
      };
      return _this;
    };
    var qr8BitByte = function(data) {
      var _mode = QRMode.MODE_8BIT_BYTE;
      var _bytes = qrcode2.stringToBytes(data);
      var _this = {};
      _this.getMode = function() {
        return _mode;
      };
      _this.getLength = function(_buffer) {
        return _bytes.length;
      };
      _this.write = function(buffer) {
        for (var i = 0; i < _bytes.length; i += 1) {
          buffer.put(_bytes[i], 8);
        }
      };
      return _this;
    };
    return qrcode2;
  })();
  return qrcode;
})());

// src/components/qr-code/qr-code.ts
var WaQrCode = class extends WebAwesomeElement {
  constructor() {
    super(...arguments);
    this.value = "";
    this.label = "";
    this.size = 128;
    this.fill = "";
    this.background = "";
    this.radius = 0;
    this.errorCorrection = "H";
    this.image = null;
    this.imageBackground = null;
    this.imageCoverage = null;
    this.imagePadding = null;
    this.computedStyle = null;
  }
  updated(changedProperties) {
    super.updated(changedProperties);
    this.generate();
  }
  generate() {
    if (!this.hasUpdated) {
      return;
    }
    this.canvas.style.maxWidth = `${this.size}px`;
    this.canvas.style.maxHeight = `${this.size}px`;
    this.computedStyle || (this.computedStyle = getComputedStyle(this));
    const computedStyle = this.computedStyle;
    const span = this.shadowRoot?.querySelector("span");
    if (span) {
      this.spanComputedStyle || (this.spanComputedStyle = getComputedStyle(span));
    }
    QrCreator.render(
      {
        text: this.value,
        radius: this.radius,
        ecLevel: this.errorCorrection,
        // Use the deprecated `fill` attribute if set, otherwise use the current text color
        fill: this.fill || computedStyle.color,
        // Use the deprecated `background` attribute if set, otherwise use transparent (the host has the bg color now)
        background: this.background || null,
        // We draw the canvas larger and scale its container down to avoid blurring on high-density displays
        size: this.size * 2,
        image: this.image,
        imageEcCover: this.imageCoverage,
        imagePadding: this.imagePadding,
        imageBackground: this.imageBackground || this.background,
        // @ts-expect-error
        cornerFill: this.spanComputedStyle?.color
      },
      this.canvas
    );
  }
  render() {
    return x`
      <canvas
        part="base qr-code"
        class="qr-code"
        role="img"
        aria-label=${this.label?.length > 0 ? this.label : this.value}
        style=${o({
      maxWidth: `${this.size}px`,
      maxHeight: `${this.size}px`,
      minWidth: `${this.size}px`,
      minHeight: `${this.size}px`
    })}
        @transitionend=${(event) => {
      if (event.propertyName === "color") {
        this.generate();
      }
    }}
      >
        <span style="color: var(--corner-color);"></span>
      </canvas>
    `;
  }
};
WaQrCode.css = qr_code_styles_default;
__decorateClass([
  e("canvas")
], WaQrCode.prototype, "canvas", 2);
__decorateClass([
  n()
], WaQrCode.prototype, "value", 2);
__decorateClass([
  n()
], WaQrCode.prototype, "label", 2);
__decorateClass([
  n({ type: Number })
], WaQrCode.prototype, "size", 2);
__decorateClass([
  n()
], WaQrCode.prototype, "fill", 2);
__decorateClass([
  n()
], WaQrCode.prototype, "background", 2);
__decorateClass([
  n({ type: Number })
], WaQrCode.prototype, "radius", 2);
__decorateClass([
  n({ attribute: "error-correction" })
], WaQrCode.prototype, "errorCorrection", 2);
__decorateClass([
  n()
], WaQrCode.prototype, "image", 2);
__decorateClass([
  n({ attribute: "image-background" })
], WaQrCode.prototype, "imageBackground", 2);
__decorateClass([
  n({ attribute: "image-coverage", type: Number })
], WaQrCode.prototype, "imageCoverage", 2);
__decorateClass([
  n({ attribute: "image-padding", type: Number })
], WaQrCode.prototype, "imagePadding", 2);
WaQrCode = __decorateClass([
  t("wa-qr-code")
], WaQrCode);

export {
  WaQrCode
};
