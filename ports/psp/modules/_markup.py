# Inline markup for picovector's text(), as on the Tufty 2350: [pen:r,g,b]
# changes the pen mid-text, and image.add_glyph(name, fn) adds more codes.
#
# picovector keeps a plain pointer to the registry dict, not a GC root, so
# main.c imports this module at startup and holds on to it: the dict then
# lives as long as the VM, whichever scripts come and go.
from picovector import color, image


def _pen(target, params, measure):
    if measure:
        return 0
    target.pen = color.rgb(*(int(c) for c in params))
    return None


GLYPHS = {"pen": _pen}
image._set_glyph_registry(GLYPHS)
