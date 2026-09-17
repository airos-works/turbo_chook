#!/usr/bin/env python3
"""Build Turbo Chook's application icon: a running Tasmanian nativehen.

The icon follows Haiku's icon guidelines: 64 x 64 canvas, light from the upper
left, a floor shadow that extends straight to the right, a one-pixel dark
silhouette at 32 x 32 (a two-unit contour on the 64-unit canvas), broad shapes
without fine detail, and mixed rather than pure colours. It writes the native
HVIF plus a host preview PNG.

    python3 tools/make-icon.py resources/branding/turbochook-icon.hvif [preview.png]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

C = hvif.hex_color


def curve(point, pin, pout):
    return (point, pin, pout)


# ------------------------------------------------------------------ styles
OUTLINE = {'color': C('2a2818')}
BODY = hvif.linear_gradient((20, 24), (44, 48), [
    (0.0, C('9a9760')), (0.45, C('6f6c3f')), (1.0, C('4a4826'))])
WING = hvif.linear_gradient((24, 28), (38, 42), [
    (0.0, C('6b683c')), (1.0, C('3f3d21'))])
HEAD = hvif.linear_gradient((41, 12), (52, 30), [
    (0.0, C('9b9868')), (0.5, C('737048')), (1.0, C('4f4d34'))])
TAIL = hvif.linear_gradient((14, 16), (22, 27), [
    (0.0, C('7f7c4c')), (1.0, C('4d4b2a'))])
BILL_TOP = {'color': C('dcd65e')}
BILL_BOTTOM = {'color': C('aaa33a')}
EYE = {'color': C('d43a2a')}
PUPIL = {'color': C('26160f')}
FLANK = {'color': C('f2efe2')}
LEG = {'color': C('5f6469')}
SHADOW = hvif.radial_gradient((39, 58), 17, [
    (0.0, (30, 30, 20, 110)), (0.7, (30, 30, 20, 45)), (1.0, (30, 30, 20, 0))], ratio=0.2)
STREAK = {'color': (120, 146, 172, 200)}

styles = [OUTLINE, BODY, WING, HEAD, TAIL, BILL_TOP, BILL_BOTTOM, EYE, PUPIL, FLANK, LEG, SHADOW, STREAK]
S = {name: index for index, name in enumerate(
    ['outline', 'body', 'wing', 'head', 'tail', 'billTop', 'billBottom', 'eye', 'pupil',
     'flank', 'leg', 'shadow', 'streak'])}

# ------------------------------------------------------------------ paths
body = {'closed': True, 'points': [
    curve((18, 30), (17, 34), (19, 26)),          # rear top
    curve((31, 23), (25, 23), (37, 23)),          # back
    curve((44, 28.5), (41, 24.5), (46.5, 32)),    # chest top / front
    curve((43, 42), (46, 38), (40, 45.5)),        # chest bottom
    curve((29, 47), (35, 47.5), (23, 46.5)),      # belly
    curve((18, 42), (19.5, 45.5), (16.5, 38)),    # rear bottom
]}
tail = {'closed': True, 'points': [
    (19.5, 29.5),
    curve((11.5, 16.5), (14.5, 22), (11, 15.5)),
    curve((13.5, 15), (12, 14.5), (15, 15.5)),
    curve((25, 25), (21, 19.5), (25, 27)),
]}
head = {'closed': True, 'points': [
    (38, 29),                                      # neck base, back
    curve((43.5, 18), (41.5, 23.5), (44, 13.5)),   # back of head
    curve((49.5, 12), (46.5, 12), (52.5, 12)),     # crown
    curve((54.5, 17), (54.5, 14), (54.5, 19.5)),   # forehead / bill root
    curve((50, 23.5), (53, 22.5), (48, 24.5)),     # chin
    (45.5, 31),                                    # neck front
]}
bill_top = {'closed': True, 'points': [(53, 16), curve((61, 19.5), (60, 18), (61, 19.5)), (53, 19.5)]}
bill_bottom = {'closed': True, 'points': [(53, 19.5), (61, 19.5), curve((53, 22.5), (56, 22.5), (53, 22.5))]}


def circle(cx, cy, r):
    k = 0.5523 * r
    return {'closed': True, 'points': [
        curve((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
        curve((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
        curve((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
        curve((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
    ]}


eye = circle(49.6, 17.2, 2.0)
pupil = circle(50.1, 17.2, 0.9)
wing = {'closed': True, 'points': [
    curve((24, 29), (22, 33), (28, 26.5)),
    curve((39, 31), (34, 27), (41, 33)),
    curve((35, 40.5), (39.5, 36), (31, 43)),
    curve((22.5, 37), (26, 42), (21.5, 34)),
]}
flank = {'closed': True, 'points': [
    curve((21.5, 40), (21, 41.5), (23.5, 39)),
    curve((39, 43.8), (31, 41.2), (41.5, 44.6)),
    curve((37.5, 45.3), (39.5, 45.3), (33, 45.3)),
    curve((22.5, 42.3), (27, 44.2), (21, 41.8)),
]}
back_leg = {'closed': False, 'points': [(27, 44), (20.5, 50), (13.5, 55.5)]}
back_toes = {'closed': False, 'points': [(9.5, 55.5), (13.5, 55.5), (14.5, 59)]}
front_leg = {'closed': False, 'points': [(37, 45), (42.5, 50.5), (48.5, 53)]}
front_toes = {'closed': False, 'points': [(48.5, 53), (54, 52), (48.5, 53), (50, 57)]}
shadow = {'closed': True, 'points': circle(39, 58, 1)['points']}  # scaled via shape matrix
streak1 = {'closed': True, 'points': [
    curve((4, 32), (4, 30.8), (4, 33.2)), curve((15, 32), (15, 33.2), (15, 30.8))]}
streak2 = {'closed': True, 'points': [
    curve((1.5, 38), (1.5, 36.8), (1.5, 39.2)), curve((11, 38), (11, 39.2), (11, 36.8))]}

paths = [body, tail, head, bill_top, bill_bottom, eye, pupil, wing, flank, back_leg, back_toes,
         front_leg, front_toes, shadow, streak1, streak2]
P = {name: index for index, name in enumerate(
    ['body', 'tail', 'head', 'billTop', 'billBottom', 'eye', 'pupil', 'wing', 'flank', 'backLeg',
     'backToes', 'frontLeg', 'frontToes', 'shadow', 'streak1', 'streak2'])}


def shape(style, *names, **extra):
    result = {'style': S[style], 'paths': [P[n] for n in names]}
    result.update(extra)
    return result


stroke_round = {'type': 'stroke', 'width': 2.5, 'join': 2, 'cap': 2, 'miter': 4}
contour = {'type': 'contour', 'width': 4, 'join': 2, 'miter': 4}

shapes = [
    # Floor shadow, straight to the right of the feet.
    shape('shadow', 'shadow', matrix=[17.0, 0.0, 0.0, 3.2, 39.0 - 39.0 * 17.0, 58.0 - 58.0 * 3.2]),
    # Motion streaks behind the bird.
    shape('streak', 'streak1', 'streak2'),
    # Legs: dark strokes act as their own outline.
    shape('leg', 'backLeg', 'backToes', 'frontLeg', 'frontToes', transformers=[stroke_round]),
    # One-pixel silhouette at 32 px: a two-unit contour on the bird's solid shapes.
    shape('outline', 'tail', transformers=[contour]),
    shape('outline', 'body', transformers=[contour]),
    shape('outline', 'head', transformers=[contour]),
    shape('outline', 'billTop', 'billBottom', transformers=[contour]),
    # Coloured shapes.
    shape('tail', 'tail'),
    shape('body', 'body'),
    shape('wing', 'wing'),
    shape('flank', 'flank'),
    shape('head', 'head'),
    shape('billBottom', 'billBottom'),
    shape('billTop', 'billTop'),
    shape('eye', 'eye'),
    shape('pupil', 'pupil'),
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
