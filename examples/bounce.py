# Bounce: coloured balls bouncing around the screen, drawn with picovector.
# Up/Down: more or fewer balls. Press START to finish.
import random

import psp
from picovector import color, font, vec2
from pspdisplay import screen, update, WIDTH, HEIGHT

BACKGROUND = color.rgb(16, 16, 24)
TEXT = color.rgb(255, 255, 255)
screen.font = font.load("fonts/sins.ppf")


def new_ball():
    r = random.randint(6, 24)
    return [
        random.randint(r, WIDTH - r), random.randint(r, HEIGHT - r),  # position
        random.choice((-3, -2, 2, 3)), random.choice((-3, -2, 2, 3)),  # speed
        r, color.hsv(random.randint(0, 255), 200, 255),  # size, colour
    ]


balls = [new_ball() for _ in range(20)]
while True:
    pressed = psp.pressed()
    if psp.START in pressed:
        break
    if psp.UP in pressed:
        balls.extend(new_ball() for _ in range(10))
    if psp.DOWN in pressed and len(balls) > 10:
        del balls[-10:]

    screen.pen = BACKGROUND
    screen.clear()
    for ball in balls:
        ball[0] += ball[2]
        ball[1] += ball[3]
        if not ball[4] <= ball[0] <= WIDTH - ball[4]:
            ball[2] = -ball[2]
        if not ball[4] <= ball[1] <= HEIGHT - ball[4]:
            ball[3] = -ball[3]
        screen.pen = ball[5]
        screen.circle(vec2(ball[0], ball[1]), ball[4])
    screen.pen = TEXT
    screen.text("{} balls   Up/Down   START finishes".format(len(balls)), vec2(4, 4))
    update()  # shows the frame, and waits for the screen to refresh
