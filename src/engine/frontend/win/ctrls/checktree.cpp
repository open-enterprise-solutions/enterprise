#include "checktree.h"

#include "backend/backend_picture.h"

// The box in each state: normal, under the mouse, pressed (the left button down), disabled. A PNG master each,
// drawn from its SVG by tools/pictures/render.js, scaled down here.
static const wxString s_unchecked_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAB1UlEQVR4nOyau0oDURCG/1kj2NmKgkYfwQtemgRBbL20CiISER9IFJEUphOjrRpJLGIsEi2sNYnR6CN4ScbdQllT5BzBYteZr1lmmVnmfJw9u8U4EI4D4agACEcFQDgqAMKJ/CZ5dCq2DMYcEyYI1IsAweAnYhSaROlSPrtvW0c2SWNj8Z5mJ6fcRU8jBLgyTl8bkeXbq8yLKdfqFeAIDsKyeA+315kup5Gyye0wJYxMxNfIwSbCBmGopy9aqdfKN+3SjGcAERb98XQ8ho3EKqID/QgS5UoVWzt7OM/mvu85xAvuJdmuzvgKMDDpj9fXVgK3eA+vJ683P+5hPWKqs9kB3f54aDCKoNLam82XSn+EIBwVAOGoAAhHBUA4KgDCUQEQjgqAcFQAhKMCIBwVAOGoAAhHBUA4KgDCUQEQjgqAcFQAhKMCIBwVYMxgPPvDu/sygkprbwyum2osdgBf+6Pt3SSqDzUEDW9MzuvtB0xFU51xVHZ4MjbvEB0ihDCas8X8xUm7HOMOKF3m0u6TMggZDBybFu9hdQjSB5bcJ54hJHjD0u94S9jkWk2LfzE8FV8i5gW3ajxw4/KMRyIUmsDRn4/L/2f0PwDCUQEQjgqAcMQL+AQAAP//zP317QAAAAZJREFUAwC7SHCva865AgAAAABJRU5ErkJggg==";
static const wxString s_uncheckedHover_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABw0lEQVR4nOyavUoDQRCAvz0j2IgggpIH8AG0UBEbxcJCFG0VLMQ38OcJBGtLGwvtBBXLpBPBRu3t86NImjRCEte9QOQi4q4gcufM1ywTZsLcx+7eFRMhnAjhqACEowIQjgpAOLmfJJd3WHfLsoVJY8iTIqylbOAWw3n+gJPQOhOS9LzNSCPi1CXPkgUshZ4c68P7PPlSg45Aw3CWmYePMcw3m5yGpHoFlLbZdNt9mozhep6r7LHhy/PeAe6PVpNx3+gY/TNL5AZHSBPNWpX69SWvj/cfv71ZVtxy/F2d/wgYppJh//Ri6h4+Ju4p7q0Ly7i3Dj8DXQVDqbr8u/jcW8ibSj+EEI4KQDgqAOGoAISjAhCOCkA4KgDhqACEowIQjgpAOCoA4agAhKMCEI4KQDgqAOGoAISjAhCOCvBmWKrJsPlSJq180VvFVxMyJfaQDOs3V7Rq3gHMP6c9Jud6S2Itd746/5SY4cjtgoVOGM/hJWfx0oyJOPTmEEBph2I8eUm2uMwfsOxLCroEey1rFopkBUsharEVkhq0AzqUd1lzy4o7WxOpG5d3G7U9Lg8Xvz4u/5/R7wCEowIQjgpAOOIFvAMAAP//QZBDqQAAAAZJREFUAwAr9VqM1gSIvwAAAABJRU5ErkJggg==";
static const wxString s_uncheckedPressed_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAB2UlEQVR4nOyavU4CQRDH/3NiYmcr2BgfATR+NBITCwvRYAudsfFxLG0spDNBsFNJ1AKxAC18AE2Uk0fwA8a9AnNQsGtizJ0zv4YMmbnM/rK7d8V4EI4H4agACEcFQDgqAMJJ/CR5bnmlCMYWExYJlEKEYHCbGI0eUblVvzxyrSOXpPn57FRvnEtm0auIAUbG+Vs3UXy4rXVsuU5HgBM4jsviA0yvaxNet+SSO2ZLyCxmd8jDHuIGYXZqeubJf368H5VmvQOIsB2O0+kMNjdzSCaTiBK+76NSqaLVan7/5xHnzc/hqDrrEWBgKRznchuRW3xA0FPQWxhzWWdsdS47YDIcp1KRuvwHGO7N5U2lH0IQjgqAcFQAhKMCIBwVAOGoAAhHBUA4KgDCUQEQjgqAcFQAhKMCIBwVAOGoAAhHBUA4KgDCUQEQjgqwZjBew2G73UZUGe6Nwb6txmEH8F04qlZP0elYBzD/nGBMLuhtAKamrc46JdYDDoyl9X4czOGFZ/EiDfX2bSnWHdC6uSqbvVRDzGCg0qxfn9nynC5B+kTBPPECMSEYlv7A+65LrtO0eJ/0crZAzHlTtRC5cXnGCxEa5sie/Pq4/H9GvwMgHBUA4agACEe8gC8AAAD//4JvA2cAAAAGSURBVAMAWC9x+uCGHksAAAAASUVORK5CYII=";
static const wxString s_uncheckedDisabled_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAABy0lEQVR4nOyaMUtCURTH/+flkC1FNJSIX6Khoq1siALFHBUaoi/U2NKQSxgV6KTS6hJ9BzEtiGjyFZWn+wjjidC9QcN7nfNbrlfO1XN/77z73nA8CMeDcFQAhKMCIBwVAOEkfhN8Xm+VGcgT8yqIUogSzD0mapvxoribPXVdRi5BZ/XrxSkMK+bjBuJBI/HO5Vwu+2ALdLoFzOariM/mA7bepqjiEmgVYMr+wAzriBlE2DS579viHM4A3gvfKQvzc8ikU5hJTiNKDPwXdLo9PD49f39nzquCGU5+WmetAPMja+F5Jr0Uuc0HBDkFuYUxh/WybZ21Agg0O/5HSUSVidwcnlT6IgThqAAIRwVAOCoAwlEBEI4KgHBUAISjAiAcFQDhqAAIRwVAOCoAwlEBEI4KgHBUAISjAiAcFQDhqABbAAP34fnA9xFVJnPjvm2NSwXchiedbh++/4qo8dUmN75fc/FubOvsXWJDHBtN26N50IcX7sWLMkx0ZItx6hWu1lrNoPMSMYLBV8WdbN4W53QIDskrgbmJ+NDwEh+HLoFOFTCiWmuWCFQwflei1i7PjDsQt82lv/zzdvn/jL4HQDgqAMJRARCOeAGfAAAA//9MdZBwAAAABklEQVQDAKQDbGz7YVK9AAAAAElFTkSuQmCC";
static const wxString s_checked_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAE9klEQVR4nOyafVAUZRzHv3u8v2lkBAEJ4qgkGcVLgpogjnI01RiN/VHQmCLC5IyNaFg2DjUxQohjryi+jGMx5Shq1GCikGDDAQo2DoKYyIkHxhBwkiQvcts+1+DcKXPPcnt37Ln3ubnZ293vs/M833ue3d/+nkcGiSODxLEZAIljMwASx2YAJI79RMQRC2KSwWIFyyCKAeMLEcGC7WRY1GgY5nhD9dnv+ZZj+IgiI2N9NA5sEdfoOFgBnBmnh0btkxtry7toWl5DgLXHUWtpPIGr6zJn2WgRH60dTRAeFZvCyPAerA0GQT5+gTduqZR/GJJR7wEMgzd09+NiY5CeuhqBAdMhJpQ32lFQeAAVZyvvH5MxbCK3OWioHHUIsEC07v66lFWiazyB1InUTRfuZh1OK8enB0zV3Q+aEQix8mDd+DypbIEQJI4tEoSVUdxSisqbCjzt4Yu3QxLh6+4NIViNAW2327H5t89wra/t/rHiq6XIj9uGhX6RMBarGAKdd/7CmtIMvcYTBu8NYmN5Fpp6rsJYRG9A790+rD25Gb2D6nHPD40O43BzCYxF1ENgYORfpJd9iA6uBxiivb8DxiJaA8g/u/70VrT0tlK1c6fNhrGI0gANq8Gmik9xsauRqvV29ULaC+/AWERpQNbv+TinqqXqPBzdUCj/XLs1FtEZ8GX9fpRcK6PqnO2cULA8FwFT/SEEURlw5MovOHDpR6rOjktQfL0sG896zYFQTGJAD/eoOqeqw+2hfoR7z+MqFoyJUnq9AtmKL3hpsxdvQcRToTAFgg1o7VMi9dQHWhPGeD9iLVbNe5P3NRQdF/BxVS4v7dboDZAHLYGpEBQI9Q/9gzUnM/QaT9h1YS921u3hdY3G7hZsKN+mvfPTSAl9CyuDX4EpEWRAbu23UHPdfjwOXT6K/LrdBsu3qpVIO5WJ4dER0FgxS471Ye/C1Agy4IyyyuD57y4XY+f5wnHPdQ10Y92vmbgzMgAaSwMWIWtRBsyBIAOmuXhSNYcaj2C74iu9Y+rBfqwu3Yi/7/ZSy4d6zUVOzEcwF4IMeG1WPC/d4SslyKst0P4mb3DpZVuo8T0h+PGZKJDnwMHOAeZC0FMg7flkVKvO41J3M1Vb1HQMGu7Tpm5Hc8+fVP30KX4oiM+Bq70LzIng12ESkMzh/ik+/NB0AjWdDVSdp/Nj2hCXbM2NYAOmOHlgf0I+bxNokLh+nzwPPm5PwhKYJCHiTiqdsEM7ZoXgZOeIb5Zvx0zPQFgKk2WEPBzdOROM7wkyLr7ftfQTPOf1DCyJSVNipCcYOxyyF2ci2i8ClsbkOcH/h0P+hLI0m15MR0LQ5My+myUpOpaoCHmCbgJ5aUoKScRkYbasMOkJe+U7DJogn7FE++Y4mZg1Le7q4ML1hDxE+YY9dO4l//nIiTVfiMsXs2eE3BxcsTs+l8v2/AxFZz2ZssZC/0gkzn4ZYsBiKbGVwa9qv2LDNj0OiWMzgKpgoffifr1NCbHyYN1YsLdoZXj0APai7t6efQfRflMFsUGWyZG66cEy9bRy1KWyYdExr8sY5hisEBaa+PrqKoPTTNQe0KCoPM5dqRxWBgv8RGs8gddNkLmHJO6KZ2AlkMXSIxhO5aPltVp8jLAFsUkMyyZypeaLbrk8iw6GQQ03vXLC5MvlH2VscQAkjs0ASBybAZA4kjfgPwAAAP//e3sOPQAAAAZJREFUAwBdqncFS/5/GQAAAABJRU5ErkJggg==";
static const wxString s_checkedHover_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAE1ElEQVR4nOyaW0wUVxjH/7MXQNh1C1JuFqSiYsFQC1oWt3ipaaIPbQmNT8XEVGsxMWnTiG3SPvhCW9LYmNbGRm3TtCVNY01rm/RFH1QKi/VGCWoUFiiG5VJYWG7dhd2dnlmj2VXcc5iZxVlnfgmZzO7/bM78Od8533fm6KBydFA5mgFQOZoBUDmaAVA5hrmInfuxnVwqeMDKcciCguB5ODmgGRx+yarDD6ztOBbRYA0yZnSoJ+IXEQvwOK03YHv6RxigSZlCYIbDzzHz8AIcXvL5UM8ipRrQW4NdZLjbEGOQPm/uex87aDrqHEB+6LXQ+4QVxTCXvwpDSgaUhM/Vj/GGU/DcunLvswCPSnL5NlI7eghwKAu9NdteVtzDCwh9EvoWBo8SajvQsYQ1SFXU5B/G/X1jWam0RAgqR8sEEWOc7O7EuQEnspNMeH3pcmQlJkEKMWNA18QYai41o2PMfe+zkz1dOLjGCltaJsQSEyHgnJrAzsazYQ8v4CHp3rsXm3B91AWxKN4Al9eDN5vOk6t31u+9/gB+6nZALIoOgUnfDPY0N6B3ajKirmdyAmJRrAFevx97L/yJm+5RqrbAkgyxKNKAACnu912y4+rwEFWbnrAA1fmFEIsiDTjQchENA31UndlgxNF1G2A2GiEWxRnw+Y1W/Hb7H6ouQa/HkbL1WGIyQwqKMuAEmc2/ab9J1elJlXO49AWsSk6BVGQxYNjjQcNgP9wzXpSkpJKOLcJc+aO3B7WtV5i0tc89jzWpaZADyQY4SHKy234OwyHr9DsFRdixLJ/5N+zEvA8vX2DSflBUjC1P5UAuJCVCY9PT2Nl0NuzhBQ5db8Vn1/5m+o22ERfe/qsRAQbtrhXPYFtuHuREkgF1bS0YJSbMxneOWzh4rSVie8e4G9X285gO0B+/IicXe1eugtxIMuBM3+2I33/vaH/oSBj4bwpvkRR3gmR7NDZnLsaB1WsRDSQZsCg+gaoRRsLH901uo9NevEGKmyGS59N4lkyon5RYES0kGfBK9tNMOqFY+bTtTjgIFdweOz2/F1hpsZC1vhxGXfRqNkmrQHV+AZoG+9A6Qi9H6zvbyUTHo2t8DDfcI1R9jsmEI9b1SDSIz/JYkGzt4dJy5C+0MGl/7OxA87+DVF1yXDyOlm1AMkOISUWyAQvj4vC1bROzCTSE/P44ye8zFiRiPpAluEykGDlu2xiMWSnE63X4ksR8nkxmsiDb7GI2xpH/nPiRIHTk0FobikSk0VKQdXoVRoLYcKgtLkVZ2vy/cpN9fbkTDpvmtEuzr3A1tsqY38+FqCywwgaFsFFR+ATdBKFoqspbjkdF1DIMYSQcW7cxoglbFmcHK8dHSVS3xRMNhuBIsD75YO1enp4Z1RSXlajvCCWRdf0rktSc6O6AnSRBwqEkG5nsKpcshRKYty2xbbnLgn9KQ3s9DpWjGUBV8OgPvfUNOaFUZukb9e0Kyymxq6G3442/w++iHsCcd4LH5EjfQiFv2C7T2tFXAQ7HyCjYevdWOIcXehZPyXA6fEHVgIHe/TgjnLxEbHEqqw4VNBHTJGjkUcWTTWDECjxO6/zYzSJlGgF3cb6HKnKpJLFVqrjj8mSgBo/LA7/Kflz+cUbLA6ByNAOgcjQDoHJUb8D/AAAA///u3hdAAAAABklEQVQDAEn7XCYt7HdNAAAAAElFTkSuQmCC";
static const wxString s_checkedPressed_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAE8UlEQVR4nOyai29TVRzHv7frWNe9gI09wY0x2HhFs0c2p8hmgoqPgaBGk2EMIUgCAycK+h8oqIgjaFxCiNlUwnRsxgRlG9sUmDGbEIeM4bKHW7uWIbRrV7t2vZ7ThaXFpefS23a33n6apjn3fs/NOd+ec+7v/u5RQOYoIHNCBkDmhAyAzAkZAJmjvB9xfvH6beCxmedQxIFLhYTgwWs4Hh0OjqvvuthaI7QeJ0RUUFCS7Ajna0mnH0cQQMw4Z51Sbuv+pVnH0gqaArwSdcHSeQpp6waVYqpWiDaMJcgrKtnBKbAbwQaHzOS0jEHt8MBlTzLmGsBx2Opazs3Nw6ZNZUhJSYGU0Gq1aGhoRFdX58wxBcdvIT8nPdVjTgEeeNi1XFb2nOQ6T6Ftom1zhSzWeax6QkZAnGs5NVVSi78b97ZNyJ0qFAhB5oQiQQQZrfqfcfnO70iMWIQnkkuREBEPMQSNARqLFp/2ncDwxMjMsbabF7Bn+Q6sjVsNbwmKKTBmHcP71z526zxl0mHFsRufo988CG+RvAFG2zgO9RyF0T4+6/lJhw0t+nZ4i6SngGXKgg+vV+Gm9ZZHne4fPbxFsgbQf/ZI73EMTQwztRnqdHiLJA1w8A4c/7MaN8b7mNqF8+Zjc9oz8BZJGnCivwZX7nQzdZFhkXg7ex/Uykh4i+QMqBs+gwtjHUzdPEU46XwFkiOTIAZJGXBe/xO+1/zI1CnIp3LFbiyNzoBYfGKAYdKIK4arMNvNyI7JQqYXDbt061d8MfCVIO3OZa8hJ3YFfIFoA0YmNDh0/RNyvzbOHHtxyfN4OmWD4Gt0G/5Add9JQdpXM15BYXw+fIWoQMhsn8B7PUfcOk85/Vc9Tg19I+ga/aYBHO39jCYymdpnU59CaeI6+BJRBtQOnoaJDPvZODvajK+H6jzWH7FocJgEOnbeDhbrEoqxdXEZfI0oAzpvd3k8/8NoCxkJ38567m/rbRzuqXJGeyzyFjyE7Znl8AeiDIgNj2Vqzo42oWbglNsxk81Eps5HMNgMzPpZ0Uuxa9l2+AtRBjySUCRI16xvw5eD09PBOmXFB73HmPE9JV29GPtzKqBU+O9uLerKNASlK3ifqZ+pPadrIcucA1rLKAbNQ0x9kioR+0mgo1Ko4E9EPw7TgGQJ+aeE0KRrxVVjD1MXo4x2hrgx4THwN6INiFKq8e7KSsEmsKDx/cGcNxAfsQCBwCcJEdrod0ij00WaQOP7N7P3IE0duHcPPssIqclIOChiJJCXGKhY/rpz1Q8kPk2J0ZHg7XSg8f2auFUIND7PCU7P4cr7ytK8/MALKIovwFzgl6RoFElQHFi5FxlRbBM2koemJ5PnbuuB37LC0yNhn0cTChfm4yXy5DiX+DUtrgpT4UDOXqyOzfnPuQfj1mBXlv9CXKH4PSNER8JbxITzunZ0G685V/u181dh/aJHIQUClhIrTXrM+ZUaodfjkDkhA5gKHqOuRY1GA6lyb9tInlHLqiNgBPC/uZYaG7+DTsfcgBlw6DY52jY3eK6TVY95F3AA1cSljXfLdB+e6148ScM5qlgS5gjoutRWT8ZSM4IMkmRv6LzYznzNJGgR5OwoJ1dsQpBAN0vbMLlTiFbQbvG75BaXlHM8v4XUKpTcdnkeIxyHDjJlz/h8u/z/mVAcAJkTMgAyJ2QAZI7sDfgXAAD//8OM7ZEAAAAGSURBVAMAno50Z5jTg3oAAAAASUVORK5CYII=";
static const wxString s_checkedDisabled_png = "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAEf0lEQVR4nOyay08bVxTGv2sPD2PeGMcG1yUBByjJqou26q6li6qVEqVZJlIXVf+hLrvpotlUVG2lZBWibrOpKrUhECAOLz+wCZiHH/g1OXcckBGIe5kZh3FmfpI1HvuMfe8399xz7pnrgs1xweY4AsDmOALA5jgCwOYoFzH+/dGT+ypwm6nqp2BsCFZCVeMqY0/p+Mfdb6d/lb2MyRj99ujvgBvVB/T2CzQHj5Wyev/WrelNkaGUC1DnZ9A8ned8VXKzBzKGQgFo2P9Ah8/RZDCGL6nt34vsJOYA9bt6T/H19yIcGkKHpx1WIpcvYG0jjq3tzPFnNF/docMv510nHAH0I5/Vn4dDQct1nsPbxNtWD03WH4uuE44ABtZz8o88sCqn2iYRqZxECDbHyQTRZCRTW3i9swtPexuGAn60t7XCCE0jAA9zC8tR5HIF7XyHXsn0FibHrqKvtwd6aQoXKBwW8f/80nHnj6hWqphfeoWDgxz0YnkBiqUSdX4RJTqeRbVaRSKVhl4s7QKVSgVzL5ZxSCPgPPKFAvRiWQH4neWdz2bzQttOrxd6saQAqqqSb0ext58V2ra2tiA8HIReLCnAUnQVO5k9oZ3b7cbNiQgUxQ29WE6A1fUYUlvbQjuXi+HGxBg8BhdmlhIgsZnGelxYxNGYGh9DV6d+3z/CFAFKxRK2d/dQLpfR3dWpq2Hp19t4ubIuZTs+OoKe7i6YgWEBsrk8ni0sUZwuH382Eh5GKHhF+je4v79YXpGyHR35AIO+fpiFoUSI3/FaklI+8fnKWgyvVjekfmP/IIvniy+lbEPDAQSvDMJMDAkQpU6Wy5Uzv4slU4iunS9CNl8bPTzsiQj4BzASMr8Sb0iA+vrbWcQTKRoJsTO/OywWMTe/TNleFSIGqA45dvVDNAJDArS2iKeQWHLz1OTGXea/54tani+CT6h80msUhgTw+wak7Hh4i76dE/gdn1sQ5/ccr9ejxXqXq3FrNkNRgFdhdyj88YlMRJzmBF5jzuUPcZATL1/bqeAxNR7Rsr1GYljaqfFReDvkKsXxZBqZXXGK26IoWoor42JGMSyAwhs7eV1aBBH8jt+YjKDNYKlLFlOciy9Gbn50XfNZQ40hX+c+b5aYUv8Jk1C0lZmxkTAZuWZKfn8RTJ1etZGg0x14qOvr7ca7xvT4UnOHCFVpOqSvuRYOmZrfX4SGBFjFrZAvy4nAF01DQT8ui4ZlGDV3OF8E30CftnK8TBpaFtdKViRCb89p3+4nf5+ghxqXTcMzDS2uU2jjtftMZl/ba9FHggT8PliBd1YSC/oHtZfVcB6Pw+Y4AogMqFiVrD/P5cWPqi6L021TE6JrZEbAv/UnaxsJ5GlNbzVq2+RO9pdu3j+i68S7xKr4mWT6+uic1wFFtUCroDL2k8hGaq/wzMMns3znJZoIFepfd7+Zvi2yk5oEq8x1j2rXs2geHruUyo8yhlIj4IiZh7P3GNgd0vcTq22Xp0cLMTD1Kd36P03fLv8+4+QBsDmOALA5jgCwObYX4A0AAAD///i2/QwAAAAGSURBVAMADFpoST+7ZysAAAAASUVORK5CYII=";

#include <wx/icon.h>
#include <wx/imaglist.h>

wxDEFINE_EVENT(wxEVT_CHECKTREE_FOCUS, wxTreeEvent);
wxDEFINE_EVENT(wxEVT_CHECKTREE_CHOICE, wxTreeEvent);

wxIMPLEMENT_DYNAMIC_CLASS(ibCheckTree, wxTreeCtrl)

bool ibCheckTree::on_check_or_label(int flags)
{
	return flags & (wxTREE_HITTEST_ONITEMSTATEICON | wxTREE_HITTEST_ONITEMLABEL) ? true : false;
}

void ibCheckTree::unhighlight(const wxTreeItemId& id)
{
	if (!id.IsOk())
		return;

	int i = ibCheckTree::GetItemState(id);

	if (ibCheckTree::UNCHECKED <= i && i < ibCheckTree::UNCHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::UNCHECKED);

	}
	else if (ibCheckTree::CHECKED <= i && i < ibCheckTree::CHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::CHECKED);
	}
}

void ibCheckTree::mohighlight(const wxTreeItemId& id, bool toggle)
{
	if (!id.IsOk())
		return;

	int i = ibCheckTree::GetItemState(id);

	if (!id.IsOk() || i < 0) {
		return;
	}

	int checkedCount = 0;
	for (auto &i : m_checkedItems) {
		checkedCount += i.second ? 1 : 0;
	}

	bool is_checked = false;

	if (ibCheckTree::UNCHECKED <= i && i < ibCheckTree::UNCHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, toggle ? ibCheckTree::CHECKED_MOUSE_OVER : ibCheckTree::UNCHECKED_MOUSE_OVER);
		is_checked = true;
	}
	else if (ibCheckTree::CHECKED <= i && i < ibCheckTree::CHECKED_DISABLED)
	{
		if (!m_allowEmpty && checkedCount <= 1)
			return;
		ibCheckTree::SetItemState(id, toggle ? ibCheckTree::UNCHECKED_MOUSE_OVER : ibCheckTree::CHECKED_MOUSE_OVER);
		is_checked = false;
	}
	else
	{
		// A state outside BOTH ranges is one of the *_DISABLED ones — an item that refuses to be
		// toggled. Nothing was set above, so there is no answer to report and nothing to file:
		// falling through announced a choice nobody made and recorded it in m_checkedItems, which
		// is also what checkedCount is counted from. The flag stayed uninitialised on that path,
		// so the value announced was whatever the stack held (Debug: Run-Time Check #3).
		return;
	}

	if (toggle)
	{
		wxTreeEvent eventChoice(wxEVT_CHECKTREE_CHOICE, this, id);
		eventChoice.SetExtraLong(is_checked ? 1 : 0);
		ibCheckTree::ProcessWindowEvent(eventChoice);

		if (m_singleCheck) {
			for (auto item : m_checkedItems) {
				if (item.second) {
					ibCheckTree::SetItemState(item.first, ibCheckTree::UNCHECKED);
				}
			}
			m_checkedItems.clear();
		}

		m_checkedItems.insert_or_assign(id, is_checked);
	}
}

void ibCheckTree::ldhighlight(const wxTreeItemId& id)
{
	if (!id.IsOk())
		return;

	int i = ibCheckTree::GetItemState(id);

	if (ibCheckTree::UNCHECKED <= i && i < ibCheckTree::UNCHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::UNCHECKED_LEFT_DOWN);
	}
	else if (ibCheckTree::CHECKED <= i && i < ibCheckTree::CHECKED_DISABLED)
	{
		ibCheckTree::SetItemState(id, ibCheckTree::CHECKED_LEFT_DOWN);
	}
}

ibCheckTree::ibCheckTree()
	:wxTreeCtrl()
	, mouse_entered_tree_with_left_down(false), last_mo(), last_ld(), last_kf(), m_singleCheck(false), m_allowEmpty(false)
{
}

ibCheckTree::ibCheckTree(wxWindow* parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
	: wxTreeCtrl(parent, id, pos, size, style & ~wxCR_MULTIPLE_CHECK & ~wxCR_SINGLE_CHECK & ~wxCR_EMPTY_CHECK)
	, mouse_entered_tree_with_left_down(false), last_mo(), last_ld(), last_kf(), m_singleCheck((style& wxCR_SINGLE_CHECK) == wxCR_SINGLE_CHECK), m_allowEmpty((style& wxCR_EMPTY_CHECK) == wxCR_EMPTY_CHECK)
{
	Init();
}

ibCheckTree::~ibCheckTree()
{
	wxTreeCtrl::UnselectAll();
	m_colors.clear();
}

void ibCheckTree::Init()
{
	wxImageList* states;

	const auto icon = [](const wxString& png) { return ibBackendPicture::GetIconFromBase64(png, wxSize(16, 16)); };

	wxIcon icons[8];
	icons[0] = icon(s_unchecked_png);
	icons[1] = icon(s_uncheckedHover_png);
	icons[2] = icon(s_uncheckedPressed_png);
	icons[3] = icon(s_uncheckedDisabled_png);
	icons[4] = icon(s_checked_png);
	icons[5] = icon(s_checkedHover_png);
	icons[6] = icon(s_checkedPressed_png);
	icons[7] = icon(s_checkedDisabled_png);

	int width = icons[0].GetWidth(),
		height = icons[0].GetHeight();

	// Make an state image list containing small icons
	states = new wxImageList(width, height, true);

	for (size_t i = 0; i < WXSIZEOF(icons); i++)
		states->Add(icons[i]);

	AssignStateImageList(states);

	Connect(wxEVT_COMMAND_TREE_SEL_CHANGED, wxTreeEventHandler(ibCheckTree::On_Tree_Sel_Changed), nullptr, this);

	Connect(wxEVT_CHAR, wxKeyEventHandler(ibCheckTree::On_Char), nullptr, this);
	Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(ibCheckTree::On_KeyDown), nullptr, this);
	Connect(wxEVT_KEY_UP, wxKeyEventHandler(ibCheckTree::On_KeyUp), nullptr, this);

	Connect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(ibCheckTree::On_Mouse_Enter_Tree), nullptr, this);
	Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(ibCheckTree::On_Mouse_Leave_Tree), nullptr, this);
	Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(ibCheckTree::On_Left_DClick), nullptr, this);
	Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ibCheckTree::On_Left_Down), nullptr, this);
	Connect(wxEVT_LEFT_UP, wxMouseEventHandler(ibCheckTree::On_Left_Up), nullptr, this);
	Connect(wxEVT_MOTION, wxMouseEventHandler(ibCheckTree::On_Mouse_Motion), nullptr, this);
	Connect(wxEVT_MOUSEWHEEL, wxMouseEventHandler(ibCheckTree::On_Mouse_Wheel), nullptr, this);

	Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(ibCheckTree::On_Tree_Focus_Set), nullptr, this);
	Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(ibCheckTree::On_Tree_Focus_Lost), nullptr, this);
}

void ibCheckTree::SetItemTextColour(const wxTreeItemId& item, const wxColour& col)
{
	std::map<wxTreeItemId, wxColor>::iterator it = m_colors.find(item);

	if (it == m_colors.end())
	{
		m_colors.insert(std::pair<wxTreeItemId, wxColor>(item, col));
	}
	else
	{
		m_colors[item] = col;
	}

	wxTreeCtrl::SetItemTextColour(item, col);
}

bool ibCheckTree::EnableCheckBox(const wxTreeItemId& item, bool enable)
{
	if (!item.IsOk())
	{
		return false;
	}

	int i = GetItemState(item);

	if (i<0 || i>CHECKED_DISABLED)
	{
		return false;
	}
	else if (enable)
	{
		if (i == UNCHECKED_DISABLED)
		{
			SetItemState(item, UNCHECKED);
		}
		else if (i == CHECKED_DISABLED)
		{
			SetItemState(item, CHECKED);
		}

		std::map<wxTreeItemId, wxColor>::iterator it = m_colors.find(item);

		if (it != m_colors.end())
		{
			SetItemTextColour(item, it->second);
			m_colors.erase(it);
		}

		return true;
	}
	else
	{
		if (i == UNCHECKED_DISABLED || i == CHECKED_DISABLED)
		{
			//don't disable a second time or we'll lose the
			//text color information.
			return true;
		}

		if (i == UNCHECKED || i == UNCHECKED_MOUSE_OVER || i == UNCHECKED_LEFT_DOWN)
		{
			SetItemState(item, UNCHECKED_DISABLED);
		}
		else if (i == CHECKED || i == CHECKED_MOUSE_OVER || i == CHECKED_LEFT_DOWN)
		{
			SetItemState(item, CHECKED_DISABLED);
		}

		wxColor col = GetItemTextColour(item);

		SetItemTextColour(item, wxColour(161, 161, 146));
		m_colors[item] = col;
		return true;
	}
}

bool ibCheckTree::DisableCheckBox(const wxTreeItemId& item)
{
	return EnableCheckBox(item, false);
}

void ibCheckTree::MakeCheckable(const wxTreeItemId& item, bool state)
{
	if (item.IsOk())
	{
		int i = GetItemState(item);

		if (i<0 || i>CHECKED_DISABLED)
		{
			SetItemState(item, state ? CHECKED : UNCHECKED);
		}
	}
}

void ibCheckTree::Check(const wxTreeItemId& item, bool state)
{
	if (item.IsOk())
	{
		int old_state = GetItemState(item);

		if (UNCHECKED <= old_state && old_state <= CHECKED_DISABLED)
		{
			bool enab = (old_state == UNCHECKED_DISABLED || old_state == CHECKED_DISABLED) ? false : true;
			int ch = enab ? CHECKED : CHECKED_DISABLED;
			int unch = enab ? UNCHECKED : UNCHECKED_DISABLED;
			int new_state = state ? ch : unch;

			int checkedCount = 0;
			for (auto i : m_checkedItems) {
				checkedCount += i.second ? 1 : 0;
			}

			if (m_singleCheck) {
				if (checkedCount > 1) {
					new_state = unch;
				}
			}

			if (!m_allowEmpty && !state && checkedCount <= 1)
				return;

			if (new_state != old_state) {
				ibCheckTree::SetItemState(item, new_state);
			}

			if (state) {
				ibCheckTree::SelectItem(item);
			}

			m_checkedItems.insert_or_assign(item, state);
		}
	}
}

void ibCheckTree::Uncheck(const wxTreeItemId& item)
{
	Check(item, false);
}

void ibCheckTree::On_Tree_Sel_Changed(wxTreeEvent& event)
{
	wxTreeItemId id = event.GetItem();

	unhighlight(last_kf);
	mohighlight(id, false);
	last_kf = id;

	event.Skip();
}

void ibCheckTree::On_Char(wxKeyEvent& event)
{
	if (!GetSelection().IsOk()) {
		//If there is no selection, any keypress should just select the first item
		wxTreeItemIdValue cookie;
		SelectItem(
			HasFlag(wxTR_HIDE_ROOT) ?
			GetFirstChild(GetRootItem(), cookie) :
			GetRootItem()
		);
		return;
	}

	event.Skip();
}

void ibCheckTree::On_KeyDown(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_SPACE) {
		ldhighlight(last_kf);
	}

	event.Skip();
}

void ibCheckTree::On_KeyUp(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_SPACE) {
		mohighlight(last_kf, true);
	}
	else if (event.GetKeyCode() == WXK_ESCAPE) {
		unhighlight(last_kf);
		last_kf = wxTreeItemId();
		Unselect();
	}

	event.Skip();
}


void ibCheckTree::On_Mouse_Enter_Tree(wxMouseEvent& event)
{
	if (event.LeftIsDown())
	{
		mouse_entered_tree_with_left_down = true;
	}
}

void ibCheckTree::On_Mouse_Leave_Tree(wxMouseEvent& event)
{
	unhighlight(last_mo);
	unhighlight(last_ld);
	last_mo = wxTreeItemId();
	last_ld = wxTreeItemId();
}

void ibCheckTree::On_Left_DClick(wxMouseEvent& event)
{
	int flags;

	HitTest(event.GetPosition(), flags);

	//double clicks on buttons can be annoying, so we'll ignore those
	//but all other double clicks will just have 1 more click added to them

	//without this, the check boxes are not as responsive as they should be
	if (!(flags & wxTREE_HITTEST_ONITEMBUTTON))
	{
		On_Left_Down(event);
		On_Left_Up(event);
	}
}

void ibCheckTree::On_Left_Down(wxMouseEvent& event)
{
	int flags;
	wxTreeItemId id = HitTest(event.GetPosition(), flags);
	if (!id.IsOk())
		return;
	int i = GetItemState(id);

	if (id.IsOk() && i >= 0 && on_check_or_label(flags))
	{
		last_ld = id;
		ldhighlight(id);

		ibCheckTree::SelectItem(id);
	}
}

void ibCheckTree::On_Left_Up(wxMouseEvent& event)
{
	SetFocus();

	int flags;
	wxTreeItemId id = HitTest(event.GetPosition(), flags);
	if (!id.IsOk())
		return;
	int i = GetItemState(id);

	if (mouse_entered_tree_with_left_down)
	{
		mouse_entered_tree_with_left_down = false;

		if (i >= 0 && on_check_or_label(flags))
		{
			mohighlight(id, false);
			last_mo = id;
		}
	}
	else if (id.IsOk())
	{
		if (flags & wxTREE_HITTEST_ONITEMBUTTON && ItemHasChildren(id))
		{
			if (IsExpanded(id))
			{
				Collapse(id);
			}
			else
			{
				Expand(id);
			}
		}
		else if (i >= 0 && on_check_or_label(flags))
		{
			if (id != last_ld)
			{
				unhighlight(last_ld);
				mohighlight(id, false);
			}
			else
			{
				mohighlight(id, true);
			}

			last_ld = wxTreeItemId();
			last_mo = id;
		}
		else
		{
			unhighlight(last_ld);
			unhighlight(last_mo);
			last_ld = wxTreeItemId();
			last_mo = wxTreeItemId();
		}
	}
	else
	{
		//id is not ok
		unhighlight(last_ld);
		unhighlight(last_mo);
		last_ld = wxTreeItemId();
		last_mo = wxTreeItemId();
	}
}

void ibCheckTree::On_Mouse_Motion(wxMouseEvent& event)
{
	if (mouse_entered_tree_with_left_down)
	{
		//just ignore everything until the left button is released
		return;
	}

	int flags;
	wxTreeItemId id = HitTest(event.GetPosition(), flags);

	if (event.LeftIsDown())
	{
		//to match the behavior of ordinary check boxes,
		//if we've moved to a new item while holding the mouse button down
		//we want to set the item where the left down click occured to have
		//mouse over highlight.  And if we return to the box where the
		//left down occured, we want to return it to the having the left down highlight

		//I don't understand why this is the behavior
		//of ordinary check boxes, but I'm goin to match it anyway.

		if (id == last_ld)
		{
			if (!last_ld.IsOk())
				return;
			int i = GetItemState(last_ld);
			if (i != UNCHECKED_LEFT_DOWN && i != CHECKED_LEFT_DOWN)
			{
				ldhighlight(last_ld);
			}
		}
		else
		{
			mohighlight(last_ld, false);
		}
	}
	else if (!id.IsOk())
	{
		unhighlight(last_mo);
		last_mo = wxTreeItemId();
	}
	else
	{
		if (!id.IsOk())
			return;

		//4 cases 1 we're still on the same item, but we've moved off the state icon or label
		//        2 we're still on the same item and on the state icon or label - do nothing
		//        3 we're on a new item but not on its state icon or label (or the new item has no state)
		//        4 we're on a new item, it has a state icon, and we're on the state icon or label

		int i = GetItemState(id);

		if (id == last_mo)
		{
			if (i < 0 || !on_check_or_label(flags))
			{
				unhighlight(last_mo);
				last_mo = wxTreeItemId();
			}
			else
			{
				//nothing
			}
		}
		else
		{
			if (i < 0 || !on_check_or_label(flags))
			{
				unhighlight(last_mo);
				last_mo = wxTreeItemId();
			}
			else
			{
				unhighlight(last_mo);
				mohighlight(id, false);
				last_mo = id;
			}
		}
	}
}

void ibCheckTree::On_Mouse_Wheel(wxMouseEvent& event)
{
	event.Skip();
}

void ibCheckTree::On_Tree_Focus_Set(wxFocusEvent& event)
{
	//event.Skip();

	//skipping this event will set the last selected item
	//to be highlighted and I want the tree items to only be
	//highlighted by keyboard actionData.
}

void ibCheckTree::On_Tree_Focus_Lost(wxFocusEvent& event)
{
	unhighlight(last_kf);
	Unselect();

	event.Skip();
}

void ibCheckTree::SetFocusFromKbd()
{
	if (last_kf.IsOk()) {
		SelectItem(last_kf);
	}

	wxTreeEvent eventChoice(wxEVT_CHECKTREE_FOCUS, this, wxTreeItemId());
	ProcessWindowEvent(eventChoice);

	wxWindow::SetFocusFromKbd();
}
