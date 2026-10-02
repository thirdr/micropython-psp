# algorithm.raycast(): a walk round a small maze, with a minimap.
import math

from picovector import algorithm, color, image, vec2
from pspdisplay import screen, WIDTH, HEIGHT


class Player:
    def __init__(self):
        self.pos = vec2(8, 8)
        self.angle = 0
        self.fov = 100

    def set_angle(self, angle):
        self.angle = angle

    def vector(self, offset=0, length=1):
        return vec2(
            math.cos((self.angle + offset) * (math.pi / 180)) * length,
            math.sin((self.angle + offset) * (math.pi / 180)) * length
        )


world_map = bytearray((
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
))


MAP_SIZE_X = 22
MAP_SIZE_Y = 16

if len(world_map) != MAP_SIZE_X * MAP_SIZE_Y:
    raise RuntimeError("Invalid map size!")


player = Player()
minimap_scale = 4
display_minimap = True

# One ray for every 2 pixels across, as the Tufty's 160 rays look on its
# pixel-doubled screen; 480 rays take twice as long.
num_rays = WIDTH // 2
column_width = WIDTH // num_rays

# The raycaster returns every wall a ray passes through, out to this distance,
# and the demo draws only the nearest, which is never more than 13.8 away on
# this walk round the maze. Looking further (the Tufty used 20) only costs time.
max_distance = 14

# Make the shades once rather than one colour per ray, every frame.
wall_pens = [color.rgb(255 - b, 255 - b, 255 - b) for b in range(256)]
minimap_pens = [color.rgb(255, 255, 255, b) for b in range(256)]


minimap_map = image(MAP_SIZE_X * minimap_scale, MAP_SIZE_Y * minimap_scale)
minimap_map.pen = color.rgb(25, 25, 25, 200)
minimap_map.clear()
minimap_map.pen = color.rgb(100, 100, 100, 100)
for x in range(MAP_SIZE_X):
    for y in range(MAP_SIZE_Y):
        if world_map[int(y * MAP_SIZE_X + x)] == 1:
            minimap_map.rectangle(x * minimap_scale, y * minimap_scale, minimap_scale, minimap_scale)

minimap_overlay = image(MAP_SIZE_X * minimap_scale, MAP_SIZE_Y * minimap_scale)
minimap_overlay_mv = memoryview(minimap_overlay)
# The Tufty clears the overlay with a viper loop; MicroPython can't compile
# viper or native code for the PSP's MIPS CPU, so copy in zeros instead.
minimap_overlay_zeros = bytes(len(minimap_overlay_mv))


d_proj = (WIDTH / 2) / math.tan(player.fov * (math.pi / 180) / 2)


def update(ticks):
    player.pos = vec2(
        math.sin(ticks / 2000) * 2 + 11,
        math.cos(ticks / 2000) * 2 + 8
    )
    player.set_angle(ticks / 30)

    if display_minimap:
        # clear the minimap overlay to 0, 0, 0, 0
        minimap_overlay_mv[:] = minimap_overlay_zeros

        minimap_pos = player.pos * minimap_scale

        minimap_overlay.pen = color.rgb(255, 255, 255)
        minimap_overlay.circle(minimap_pos, 1)

        minimap_overlay.pen = color.rgb(255, 255, 255, 150)
        minimap_overlay.line(minimap_pos, minimap_pos + player.vector(offset=-player.fov / 2, length=2.5 * minimap_scale))
        minimap_overlay.line(minimap_pos, minimap_pos + player.vector(offset=player.fov / 2, length=2.5 * minimap_scale))

    # draw the sky
    screen.pen = color.rgb(128, 128, 255)
    screen.rectangle(0, 0, WIDTH, HEIGHT / 2)

    # cast rays for player sight
    result = algorithm.raycast(player.pos, math.radians(player.angle), player.fov, num_rays, max_distance, world_map, MAP_SIZE_X, MAP_SIZE_Y, WIDTH)

    for ray_x, ray in enumerate(result):
        for (tile_id, cb_p, _cb_g, _edge, _offset, distance, _ray_angle) in ray:
            if tile_id == 1:
                height = (2 / distance) * d_proj
                b = min(255, int(distance * 20))
                screen.pen = wall_pens[b]
                screen.rectangle(ray_x * column_width, (HEIGHT / 2) - (height / 2), column_width, height)

                if display_minimap:
                    minimap_overlay.pen = minimap_pens[b]
                    minimap_overlay.put(cb_p * minimap_scale)
                break

    if display_minimap:
        # draw the minimap over the top, in the top-right corner, clear of the
        # demos menu at the bottom
        corner = vec2(WIDTH - minimap_map.width - 4, 16)
        screen.blit(minimap_map, corner)
        screen.blit(minimap_overlay, corner)
