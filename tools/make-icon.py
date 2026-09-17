#!/usr/bin/env python3
"""Build Turbo Chook's application icon: a running Tasmanian nativehen.

Second design, made to read from 16 x 16 up to 128 x 128 and to follow
Haiku's icon guidelines more closely:

- 64 x 64 canvas with the bird filling it; broad shapes, thick legs, a big
  head and eye so the silhouette survives at 16 px.
- Light from the upper left (every gradient runs light top-left to dark
  bottom-right); a soft floor shadow that extends straight to the right.
- Level of detail: the fine parts (wing, flank streak, pupil highlight,
  motion streaks, dust) only render at 32 px and above; below that the bird
  is a clean silhouette with a red eye and yellow bill. The dark outline is
  one pixel at 32 x 32 and drops to a thinner rim at 16 x 16.
- Mixed colours rather than pure ones, no fine hairlines.

    python3 tools/make-icon.py resources/branding/turbochook-icon.hvif [preview.png]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

C = hvif.hex_color


def curve(point, pin, pout):
    return (point, pin, pout)


def circle(cx, cy, r):
    k = 0.5523 * r
    return {'closed': True, 'points': [
        curve((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
        curve((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
        curve((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
        curve((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
    ]}


# ------------------------------------------------------------------ styles
OUTLINE = {'color': C('2b2919')}
BODY = hvif.linear_gradient((14, 18), (46, 50), [
    (0.0, C('a4a168')), (0.5, C('737040')), (1.0, C('4a4826'))])
HEAD = hvif.linear_gradient((38, 6), (58, 28), [
    (0.0, C('aaa775')), (0.55, C('787551')), (1.0, C('514f37'))])
TAIL = hvif.linear_gradient((8, 10), (22, 26), [
    (0.0, C('8d8a58')), (1.0, C('4d4b2a'))])
WING = hvif.linear_gradient((20, 24), (40, 42), [
    (0.0, C('6d6a3e')), (1.0, C('3d3b20'))])
BILL_TOP = hvif.linear_gradient((54, 14), (62, 20), [(0.0, C('ecdf72')), (1.0, C('cfc44c'))])
BILL_BOTTOM = {'color': C('a89e34')}
EYE = {'color': C('d63b2a')}
PUPIL = {'color': C('241410')}
GLINT = {'color': (255, 240, 230, 230)}
FLANK = {'color': C('f3f0e2')}
LEG = {'color': C('5c6166')}
LEG_LIGHT = {'color': C('7e848a')}
SHADOW = hvif.radial_gradient((37, 59), 20, [
    (0.0, (30, 30, 20, 120)), (0.65, (30, 30, 20, 50)), (1.0, (30, 30, 20, 0))], ratio=0.18)
STREAK = {'color': (118, 146, 176, 210)}
DUST = {'color': (170, 165, 140, 150)}

style_names = ['outline', 'body', 'head', 'tail', 'wing', 'billTop', 'billBottom', 'eye', 'pupil', 'glint',
               'flank', 'leg', 'legLight', 'shadow', 'streak', 'dust']
styles = [OUTLINE, BODY, HEAD, TAIL, WING, BILL_TOP, BILL_BOTTOM, EYE, PUPIL, GLINT, FLANK, LEG, LEG_LIGHT, SHADOW, STREAK, DUST]
S = {name: index for index, name in enumerate(style_names)}

# ------------------------------------------------------------------ paths
body = {'closed': True, 'points': [
    curve((14, 29), (13, 34), (15, 24)),          # rear top
    curve((29, 19), (22, 19), (36, 19)),          # back
    curve((45, 26), (41, 21), (48, 30)),          # chest top
    curve((44, 42), (48, 37), (41, 46)),          # chest bottom
    curve((27, 48), (34, 48.5), (20, 47.5)),      # belly
    curve((14, 42), (16, 46), (12.5, 37)),        # rear bottom
]}
tail = {'closed': True, 'points': [
    (17, 30),
    curve((7, 14), (10, 21), (6.5, 12.5)),
    curve((10, 12), (8, 11.5), (12, 12.5)),
    curve((23, 25), (19, 18), (23, 27)),
]}
head = {'closed': True, 'points': [
    (35, 28),
    curve((41, 14), (38.5, 21), (41.5, 9.5)),
    curve((49, 8), (45, 8), (53, 8)),
    curve((56, 14.5), (56, 11), (56, 18)),
    curve((51, 23), (54.5, 21), (48, 24.5)),
    (45, 31),
]}
bill_top = {'closed': True, 'points': [(54.5, 13), curve((63, 17.5), (61.5, 15.5), (63, 17.5)), (54.5, 17.5)]}
bill_bottom = {'closed': True, 'points': [(54.5, 17.5), (63, 17.5), curve((54.5, 21.5), (58.5, 21.5), (54.5, 21.5))]}
eye = circle(49.5, 15, 2.8)
eye_small = circle(49.5, 15, 3.4)
pupil = circle(50.2, 15.2, 1.4)
glint = circle(49.2, 14.1, 0.6)
wing = {'closed': True, 'points': [
    curve((21, 26), (19, 30), (25, 23.5)),
    curve((38, 28), (33, 24), (41, 31)),
    curve((34, 39), (39, 34.5), (30, 41.5)),
    curve((19.5, 35), (23.5, 40), (18.5, 31.5)),
]}
flank = {'closed': True, 'points': [
    curve((18, 40), (17.5, 42), (20, 38.5)),
    curve((38, 43.5), (29, 41), (41, 44.5)),
    curve((36, 45.8), (38.5, 45.8), (31, 45.8)),
    curve((19, 42.8), (24, 45), (17.5, 41.8)),
]}
back_leg = {'closed': False, 'points': [(24, 45), (17, 52), (11, 57)]}
back_toes = {'closed': False, 'points': [(6.5, 57.5), (11, 57), (12.5, 61.5)]}
front_leg = {'closed': False, 'points': [(36, 46), (41.5, 52), (48, 54.5)]}
front_toes = {'closed': False, 'points': [(48, 54.5), (54.5, 53.5), (48, 54.5), (50, 59)]}
shadow = circle(37, 59, 1)
streak1 = {'closed': True, 'points': [curve((2, 31), (2, 29.6), (2, 32.4)), curve((13, 31), (13, 32.4), (13, 29.6))]}
streak2 = {'closed': True, 'points': [curve((0.5, 37.5), (0.5, 36.1), (0.5, 38.9)), curve((10, 37.5), (10, 38.9), (10, 36.1))]}
dust1 = circle(5, 55, 2.2)
dust2 = circle(1.5, 59, 1.5)

path_names = ['body', 'tail', 'head', 'billTop', 'billBottom', 'eye', 'eyeSmall', 'pupil', 'glint', 'wing', 'flank', 'backLeg',
              'backToes', 'frontLeg', 'frontToes', 'shadow', 'streak1', 'streak2', 'dust1', 'dust2']
paths = [body, tail, head, bill_top, bill_bottom, eye, eye_small, pupil, glint, wing, flank, back_leg, back_toes, front_leg,
         front_toes, shadow, streak1, streak2, dust1, dust2]
P = {name: index for index, name in enumerate(path_names)}


def shape(style, *names, **extra):
    result = {'style': S[style], 'paths': [P[n] for n in names]}
    result.update(extra)
    return result


leg_stroke = {'type': 'stroke', 'width': 3, 'join': 2, 'cap': 2, 'miter': 4}
leg_small = {'type': 'stroke', 'width': 4, 'join': 2, 'cap': 2, 'miter': 4}
leg_outline = {'type': 'stroke', 'width': 5, 'join': 2, 'cap': 2, 'miter': 4}
leg_light = {'type': 'stroke', 'width': 1, 'join': 2, 'cap': 2, 'miter': 4}
contour_large = {'type': 'contour', 'width': 4, 'join': 2, 'miter': 4}   # 1 px at 32 x 32
contour_small = {'type': 'contour', 'width': 2, 'join': 2, 'miter': 4}   # a faint rim at 16 x 16

SMALL = {'lod': (0.0, 0.5)}      # below 32 px
LARGE = {'lod': (0.5, 4.0)}      # 32 px and up
DETAIL = {'lod': (0.95, 4.0)}    # 64 px and up

shapes = [
    shape('shadow', 'shadow', matrix=[18.0, 0.0, 0.0, 3.4, 37.0 - 37.0 * 18.0, 59.0 - 59.0 * 3.4]),
    shape('dust', 'dust1', 'dust2', **DETAIL),
    shape('streak', 'streak1', 'streak2', **LARGE),
    shape('outline', 'backLeg', 'backToes', 'frontLeg', 'frontToes', transformers=[leg_outline], **LARGE),
    shape('leg', 'backLeg', 'backToes', 'frontLeg', 'frontToes', transformers=[leg_stroke], **LARGE),
    shape('leg', 'backLeg', 'backToes', 'frontLeg', 'frontToes', transformers=[leg_small], **SMALL),
    shape('legLight', 'backLeg', 'frontLeg', transformers=[leg_light], **DETAIL),
    shape('outline', 'tail', transformers=[contour_large], **LARGE),
    shape('outline', 'body', transformers=[contour_large], **LARGE),
    shape('outline', 'head', transformers=[contour_large], **LARGE),
    shape('outline', 'billTop', 'billBottom', transformers=[contour_large], **LARGE),
    shape('outline', 'body', transformers=[contour_small], **SMALL),
    shape('outline', 'head', transformers=[contour_small], **SMALL),
    shape('tail', 'tail'),
    shape('body', 'body'),
    shape('wing', 'wing', **LARGE),
    shape('flank', 'flank', **LARGE),
    shape('head', 'head'),
    shape('billBottom', 'billBottom'),
    shape('billTop', 'billTop'),
    shape('eye', 'eye', **LARGE),
    shape('eye', 'eyeSmall', **SMALL),
    shape('pupil', 'pupil', **LARGE),
    shape('glint', 'glint', **DETAIL),
]

icon = {'styles': styles, 'paths': paths, 'shapes': shapes}

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'turbochook-icon.hvif'
    data = hvif.encode(icon)
    hvif.decode(data)
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes' % (out, len(data)))
    svg = out.rsplit('.', 1)[0] + '.svg'
    with open(svg, 'w') as f:
        f.write(hvif.to_svg(icon))
    print('%s: SVG source' % svg)
    if len(sys.argv) > 2:
        hvif.preview(icon, 256, (245, 245, 240, 255)).save(sys.argv[2])
        print(sys.argv[2])
