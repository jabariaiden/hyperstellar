import hyperstellar as se
import math
from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from hyperstellar.typed import *
# ---------------------------------------------------------------------
# Simulation setup
# ---------------------------------------------------------------------
sim = se.Simulation(
    headless=False,
    width=1400, height=900,
    title="Schwarzschild GR Lensing (JIT)",
    enable_grid=False
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

sim.set_paint_resolution(1200, 800)
sim.set_camera_zoom(12.0)

CAM = 0
sim.create_scratchpad(2)

# ---------------------------------------------------------------------
# JIT paint shader
# ---------------------------------------------------------------------
@sim.script(mode="paint")
def blackhole_gr():
    # ---- constants ----
    M = 1.0
    DISK_LUM = 20.0
    EXPOSURE = 0.4
    INNER = (1.0, 0.1, 0.05)   # blue‑white
    OUTER = (1.0, 0.1, 0.05)   # orange‑red

    # ---- camera from scratchpad ----
    yaw = scratchpad_read(CAM, 0)
    pitch = scratchpad_read(CAM, 1)
    R = 18.0
    eye = (R * sin(pitch) * cos(yaw),
           R * cos(pitch),
           R * sin(pitch) * sin(yaw))
    ray = camera_ray(eye, (0.0, 0.0, 0.0), (0.0, 1.0, 0.0), 53.13)
    o = ray.origin
    d = ray.direction

    # ---- geodesic integration ----
    r0 = length(o)
    rh = normalize(o)
    rad0 = dot(d, rh)
    tang_vec = d - rad0 * rh
    tang = length(tang_vec)
    f0 = 1.0 - 2.0 * M / r0

    radial = 1 if tang < 1e-6 else 0

    if radial == 0:
        b = r0 * tang / sqrt(max(f0, 1e-6))
        e0 = rh
        ep = tang_vec * (1.0 / tang)          # avoid vector/float division
    else:
        b = 0.0
        e0 = rh
        ep = (0.0, 0.0, 0.0)

    e0x, e0y, e0z = e0
    epx, epy, epz = ep

    u = 1.0 / r0
    du = -rad0 / b if radial == 0 else 0.0
    phi = 0.0

    escaped = 0
    captured = 0
    disk_hit = 0
    disk_r = -1.0
    dpx = 0.0
    dpz = 0.0
    ddir = (0.0, 0.0, 0.0)
    final_dir = (0.0, 0.0, 0.0)

    if radial == 1:
        if rad0 < 0.0:
            captured = 1
        else:
            escaped = 1
            final_dir = normalize(d)

    if radial == 0:
        prev_u = u
        prev_phi = phi
        prev_r = r0
        prev_du = du
        for _ in range(512):
            if captured != 0 or escaped != 0 or disk_hit != 0:
                break

            cr = 1.0 / max(u, 1e-6)
            if cr <= 2.0005 * M:
                captured = 1
                break
            if cr > 80.0 * M and du < 0.0:
                escaped = 1
                dr = -b * du
                ef = max(1.0 - 2.0 * M / cr, 1e-6)
                trans = b * sqrt(ef) / cr
                c, s = cos(phi), sin(phi)
                er = (c * e0x + s * epx,
                      c * e0y + s * epy,
                      c * e0z + s * epz)
                epr = (-s * e0x + c * epx,
                       -s * e0y + c * epy,
                       -s * e0z + c * epz)
                fd = (dr * er[0] + trans * epr[0],
                      dr * er[1] + trans * epr[1],
                      dr * er[2] + trans * epr[2])
                final_dir = normalize(fd)
                break

            # adaptive step size
            if cr >= 12.0 * M:
                dphi = 0.10
            elif cr >= 5.0 * M:
                dphi = 0.035
            else:
                dphi = 0.012

            mid_u = u + 0.5 * dphi * du
            mid_du = du + 0.5 * dphi * (3.0 * M * u * u - u)
            new_u = u + dphi * mid_du
            new_du = du + dphi * (3.0 * M * mid_u * mid_u - mid_u)
            new_phi = phi + dphi

            prev_u, prev_phi, prev_r, prev_du = u, phi, cr, du
            u, du, phi = new_u, new_du, new_phi

            if u <= 0.0:
                escaped = 1
                break

            nr = 1.0 / u
            c, s = cos(phi), sin(phi)
            er = (c * e0x + s * epx,
                  c * e0y + s * epy,
                  c * e0z + s * epz)
            epr = (-s * e0x + c * epx,
                   -s * e0y + c * epy,
                   -s * e0z + c * epz)
            cp = (nr * er[0], nr * er[1], nr * er[2])

            old_y = prev_r * (cos(prev_phi) * e0y + sin(prev_phi) * epy)
            new_y = cp[1]

            # disk crossing
            if (old_y > 0.0 and new_y <= 0.0) or (old_y < 0.0 and new_y >= 0.0):
                den = old_y - new_y
                if abs(den) > 1e-6:
                    hit_t = clamp(old_y / den, 0.0, 1.0)
                    hit_u = prev_u * (1.0 - hit_t) + u * hit_t
                    hit_phi = prev_phi * (1.0 - hit_t) + phi * hit_t
                    hit_r = 1.0 / max(hit_u, 1e-6)
                    if hit_r >= 6.0 * M and hit_r <= 12.0 * M:
                        disk_r = hit_r
                        hc, hs = cos(hit_phi), sin(hit_phi)
                        her = (hc * e0x + hs * epx,
                               hc * e0y + hs * epy,
                               hc * e0z + hs * epz)
                        hep = (-hs * e0x + hc * epx,
                               -hs * e0y + hc * epy,
                               -hs * e0z + hc * epz)
                        dpx = hit_r * her[0]
                        dpz = hit_r * her[2]
                        lf = max(1.0 - 2.0 * M / disk_r, 1e-6)
                        hit_du = prev_du * (1.0 - hit_t) + du * hit_t
                        nr_comp = -b * hit_du
                        nphi = b * sqrt(lf) / disk_r
                        pad = (nr_comp * her[0] + nphi * hep[0],
                               nr_comp * her[1] + nphi * hep[1],
                               nr_comp * her[2] + nphi * hep[2])
                        plen = length(pad)
                        if plen > 1e-6:
                            ddir = (-pad[0] / plen,
                                    -pad[1] / plen,
                                    -pad[2] / plen)
                            disk_hit = 1

    # ---- background stars ----
    col = (0.005, 0.005, 0.015)

    if captured == 0 and disk_hit == 0:
        fd = final_dir if escaped != 0 else normalize(d)

        s0 = dot(fd, (127.1, 311.7, 74.7))
        s1 = dot(fd, (269.5, 183.3, 246.1))
        star_val = fract(sin(s0 * 12.9898 + s1 * 78.233) * 43758.5453)
        star = step(0.9975, star_val)

        h0 = dot(fd, (53.1, 97.3, 63.7))
        h1 = dot(fd, (34.7, 89.3, 21.5))
        star_hue = fract(sin(h0 * 12.9898 + h1 * 78.233) * 43758.5453)
        col = (col[0] + star * (0.55 + 0.45 * star_hue),
               col[1] + star * (0.55 + 0.45 * (1.0 - star_hue)),
               col[2] + star)

        bs0 = dot(fd, (89.3, 213.7, 37.1))
        bs1 = dot(fd, (98.1, 47.3, 156.7))
        bright_val = fract(sin(bs0 * 12.9898 + bs1 * 78.233) * 43758.5453)
        bright = step(0.9995, bright_val) * 1.5
        col = (col[0] + bright * 0.9,
               col[1] + bright * 0.8,
               col[2] + bright)

    # ---- accretion disk ----
    if disk_hit == 1:
        r = disk_r
        v_mag = clamp(sqrt(M / max(r - 2.0 * M, 1e-6)), 0.0, 0.999)
        tangent = normalize((-dpx, 0.0, dpz))
        vel = v_mag * tangent
        beta_dot_n = dot(vel, ddir)
        gamma = 1.0 / sqrt(max(1.0 - v_mag * v_mag, 1e-6))
        grav = sqrt(max(1.0 - 2.0 * M / r, 1e-6))
        g_factor = clamp(grav / max(gamma * (1.0 - beta_dot_n), 1e-6), 0.0, 3.0)
        intensity = DISK_LUM * g_factor * g_factor * g_factor

        t_disk = saturate((r - 6.0) / 10.0)   # 0 at 6M, 1 at 16M
        disk_col = mix(INNER, OUTER, t_disk)
        col = (disk_col[0] * intensity,
               disk_col[1] * intensity,
               disk_col[2] * intensity)

    # ---- exposure and output ----
    col = (clamp(col[0] * EXPOSURE, 0.0, 1.0),
           clamp(col[1] * EXPOSURE, 0.0, 1.0),
           clamp(col[2] * EXPOSURE, 0.0, 1.0))

    # assign color components individually
    color.r = col[0]
    color.g = col[1]
    color.b = col[2]
    color.a = 1.0

# ---------------------------------------------------------------------
# Run the simulation
# ---------------------------------------------------------------------
sim.paint(blackhole_gr._script_id)
sim.set_camera_zoom(3)

print("Schwarzschild GR Lensing (JIT)")
print("Controls: Left/Right : orbit yaw, Up/Down : orbit pitch")

yaw, pitch = 0.5, 1.7
step = 0.01

while not sim.should_close():
    sim.default_input()
    sim.process_input()

    if sim.keyboard.Left.pressed:   yaw -= step
    if sim.keyboard.Right.pressed:  yaw += step
    if sim.keyboard.Up.pressed:     pitch += step
    if sim.keyboard.Down.pressed:   pitch -= step
    pitch = max(0.01, min(math.pi - 0.01, pitch))

    sim.upload_scratchpad(CAM, [yaw, pitch])
    sim.update(0.016)
    sim.render()