#!/usr/bin/env python3
import hyperstellar as se, time, math

sim = se.Simulation(headless=False, title="3D Raycasting Example")
while not sim.are_all_shaders_ready():
    sim.update_shader_loading(); time.sleep(0.01)

sim.set_paint_resolution(1400, 900)
sim.paint("color.r = 0.05; color.g = 0.05; color.b = 0.08; color.a = 1.0")
sim.update(0.001); sim.render()

rot_obj = sim.add_object(x=0.0, y=0.5, r=1, g=1, b=1, a=0.0, size=1, skin=se.SkinType.CIRCLE)
sim.set_equation(rot_obj, "ax=0; ay=0; angular=0; color.a=0")

@sim.script(mode='paint')
def raycast_3d():
    yaw = p[rot_obj].x
    pitch = p[rot_obj].y
    dist = 5.0
    center = [0.0, 0.0, 0.0]

    origin = [
        center[0] + dist * sin(pitch) * cos(yaw),
        center[1] + dist * cos(pitch),
        center[2] + dist * sin(pitch) * sin(yaw)
    ]

    fwd = normalize([center[0]-origin[0], center[1]-origin[1], center[2]-origin[2]])
    right = normalize([fwd[2], 0.0, -fwd[0]])
    up = cross(right, fwd)

    ray = normalize([
        px*right[0] + py*up[0] + 3.5*fwd[0],
        px*right[1] + py*up[1] + 3.5*fwd[1],
        px*right[2] + py*up[2] + 3.5*fwd[2]
    ])
    light = normalize([0.6, 0.8, 1.0])

    bg = (py + 1.0) * 0.5
    color = [0.02+bg*0.05, 0.03+bg*0.08, 0.08+bg*0.15, 1.0]

    radius = 1.0
    oc = [origin[0]-center[0], origin[1]-center[1], origin[2]-center[2]]
    b = dot(oc, ray)
    c = dot(oc, oc) - radius*radius
    disc = b*b - c
    t_sphere = -1.0
    if disc > 0.0:
        t_sphere = -b - sqrt(disc)

    t_floor = -1.0
    if abs(ray[2]) > 0.0001:
        t_floor = (-1.0 - origin[2]) / ray[2]
        if t_floor < 0.0:
            t_floor = -1.0

    if t_sphere > 0.0 and (t_floor < 0.0 or t_sphere < t_floor):
        hit = [origin[0]+t_sphere*ray[0], origin[1]+t_sphere*ray[1], origin[2]+t_sphere*ray[2]]
        n = normalize([hit[0]-center[0], hit[1]-center[1], hit[2]-center[2]])
        diff = max(0.0, dot(n, light))
        ambient = 0.12
        refl = reflect([-light[0], -light[1], -light[2]], n)
        view = [-ray[0], -ray[1], -ray[2]]
        spec = pow(max(0.0, dot(view, refl)), 32.0)
        rim = pow(1.0 - max(0.0, dot(n, view)), 3.0) * 0.5
        sc = [0.2, 0.6, 1.0]
        col = [
            sc[0]*(ambient+diff*0.85) + spec*0.4 + rim*0.3,
            sc[1]*(ambient+diff*0.85) + spec*0.4 + rim*0.6,
            sc[2]*(ambient+diff*0.85) + spec*0.4 + rim*1.0
        ]
        color = [col[0], col[1], col[2], 1.0]
    elif t_floor > 0.0:
        hit = [origin[0]+t_floor*ray[0], origin[1]+t_floor*ray[1], origin[2]+t_floor*ray[2]]
        check_x = int(floor(hit[0]*2.0))
        check_y = int(floor(hit[1]*2.0))
        even = mod(float(check_x+check_y), 2.0)
        f_color = 0.25 if even < 0.5 else 0.45
        to_light = [center[0]-hit[0], center[1]-hit[1], center[2]-hit[2]]
        t_proj = dot(to_light, light)
        shadow = 1.0
        if t_proj > 0.0:
            d2 = dot(to_light, to_light) - t_proj*t_proj
            d = sqrt(max(0.0, d2))
            r_core = 1.0
            r_penumbra = 1.25
            if d < r_penumbra:
                edge = clamp((d - r_core)/(r_penumbra - r_core), 0.0, 1.0)
                shadow = 0.3 + 0.7*edge
        floor_col = [f_color*shadow*0.7, f_color*shadow*0.8, f_color*shadow*1.0]
        color = [floor_col[0], floor_col[1], floor_col[2], 1.0]

sim.paint(raycast_3d._script_id)
sim.set_speed(1.0)

print("Controls: Left/Right: yaw, Up/Down: pitch, WASD/QE/Scroll: camera")

rot_x, rot_y = 0.0, 0.5
step, pitch_min, pitch_max = 0.05, 0.01, math.pi - 0.01

while not sim.should_close():
    sim.process_input()
    if sim.keyboard.Left.pressed:  rot_x -= step
    if sim.keyboard.Right.pressed: rot_x += step
    if sim.keyboard.Up.pressed:    rot_y += step
    if sim.keyboard.Down.pressed:  rot_y -= step
    rot_y = max(pitch_min, min(pitch_max, rot_y))
    sim.update_object(rot_obj, x=rot_x, y=rot_y)
    sim.update(0.016)
    sim.render()

sim.cleanup()