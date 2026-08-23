import hyperstellar as se
import math

# Setup
sim = se.Simulation(
    headless=False,
    width=1400,
    height=900,
    title="Schwarzschild GR Lensing",
    enable_grid=False
)

while not sim.are_all_shaders_ready():
    sim.update_shader_loading()

# Set paint resolution – higher values improve quality but slow down.
sim.set_paint_resolution(1200, 800)
sim.set_camera_zoom(12.0)          # initial zoom (can be adjusted later)

# 1. Scratchpad and Agent setup for camera control
# Allocate scratchpad slot 0 for camera parameters: [yaw, pitch]
CAMERA_SCRATCH_ID = 0
sim.create_scratchpad(2)            # allocate 2 floats

# 2. Paint Script – Black Hole Ray Tracer
@sim.script(mode="paint", )
def blackhole_gr():
    """
    This GPU shader traces null geodesics in Schwarzschild spacetime,
    computes emission from a thin accretion disk (6M–16M), and adds
    background stars. The camera is set up using the camera_ray macro.
    """
    # USER CONTROLS – change these freely
    M = 1.0                     # black hole mass (affects shadow & disk radii)
    DISK_LUMINOSITY = 20.0      # intrinsic emitted intensity (linear)
    EXPOSURE = 0.4              # final display brightness multiplier

    # Radial colour gradient: inner (at 6M) and outer (at 16M), each in [0,1].
    inner_colour_r = 1.0        # hot blue‑white
    inner_colour_g = 0.1
    inner_colour_b = 0.05

    outer_colour_r = 1.0        # warm orange‑red
    outer_colour_g = 0.1
    outer_colour_b = 0.05

    # Camera parameters read from scratchpad instead of object p[rot_obj]
    yaw   = scratchpad_read(CAMERA_SCRATCH_ID, 0)
    pitch = scratchpad_read(CAMERA_SCRATCH_ID, 1)
    camera_r = 18.0

    # Compute eye position (spherical coordinates).
    eye = (
        camera_r * sin(pitch) * cos(yaw),
        camera_r * cos(pitch),
        camera_r * sin(pitch) * sin(yaw)
    )

    # The target is the origin (black hole).
    target = (0.0, 0.0, 0.0)

    # Up vector (world up).
    up = (0.0, 1.0, 0.0)

    # FOV from the original focal length 2.0: tan(half‑angle) = 1/2.
    FOV = 53.13   # degrees

    # Use the macro – it expands to a call that uses px, py automatically.
    ray = camera_ray(eye, target, up, FOV)
    ray_dir = ray.direction
    origin = ray.origin

    # Extract components for convenience.
    ox, oy, oz = origin[0], origin[1], origin[2]
    ray0, ray1, ray2 = ray_dir[0], ray_dir[1], ray_dir[2]

    # Initial geodesic parameters
    r0 = sqrt(ox*ox + oy*oy + oz*oz)
    rh0 = ox / r0; rh1 = oy / r0; rh2 = oz / r0

    rad0 = ray0*rh0 + ray1*rh1 + ray2*rh2
    t0 = ray0 - rad0*rh0
    t1 = ray1 - rad0*rh1
    t2 = ray2 - rad0*rh2
    tang = sqrt(t0*t0 + t1*t1 + t2*t2)
    f0 = 1.0 - 2.0*M/r0
    radial_flag = 0.0
    if tang < 1e-6:
        radial_flag = 1.0

    b = 0.0
    if radial_flag == 0.0:
        b = r0 * tang / sqrt(max(f0, 1e-6))

    e0_0 = rh0; e0_1 = rh1; e0_2 = rh2
    ep0 = 0.0; ep1 = 0.0; ep2 = 0.0
    if radial_flag == 0.0:
        tl = sqrt(t0*t0 + t1*t1 + t2*t2)
        ep0 = t0 / tl; ep1 = t1 / tl; ep2 = t2 / tl

    u = 1.0 / r0
    du = 0.0
    if radial_flag == 0.0:
        du = -rad0 / b

    phi = 0.0
    pd0 = ray0; pd1 = ray1; pd2 = ray2
    escaped = 0.0
    captured = 0.0

    disk_hit = 0.0
    disk_r = -1.0
    dpx = 0.0; dpy = 0.0; dpz = 0.0
    ddir0 = 0.0; ddir1 = 0.0; ddir2 = 0.0

    if radial_flag == 1.0:
        if rad0 < 0.0:
            captured = 1.0
        else:
            escaped = 1.0
            pd0 = ray0; pd1 = ray1; pd2 = ray2

    if radial_flag == 0.0:
        prev_u = u; prev_phi = phi; prev_r = r0; prev_du = du
        for _ in range(512):
            if captured == 1.0 or escaped == 1.0 or disk_hit == 1.0:
                break
            cr = 1.0 / max(u, 1e-6)
            if cr <= 2.0005*M:
                captured = 1.0; break
            if cr > 80.0*M and du < 0.0:
                escaped = 1.0
                dr = -b * du
                ef = max(1.0 - 2.0*M/cr, 1e-6)
                trans = b * sqrt(ef) / cr
                c = cos(phi); s = sin(phi)
                er0 = c*e0_0 + s*ep0
                er1 = c*e0_1 + s*ep1
                er2 = c*e0_2 + s*ep2
                epr0 = -s*e0_0 + c*ep0
                epr1 = -s*e0_1 + c*ep1
                epr2 = -s*e0_2 + c*ep2
                fd0 = dr*er0 + trans*epr0
                fd1 = dr*er1 + trans*epr1
                fd2 = dr*er2 + trans*epr2
                flen = sqrt(fd0*fd0 + fd1*fd1 + fd2*fd2)
                pd0 = fd0/flen; pd1 = fd1/flen; pd2 = fd2/flen
                break

            dphi = 0.10
            if cr < 12.0*M: dphi = 0.035
            if cr < 5.0*M:  dphi = 0.012

            mid_u = u + 0.5*dphi*du
            mid_du = du + 0.5*dphi*(3.0*M*u*u - u)
            new_u = u + dphi*mid_du
            new_du = du + dphi*(3.0*M*mid_u*mid_u - mid_u)
            new_phi = phi + dphi

            prev_u = u; prev_phi = phi; prev_r = cr; prev_du = du
            u = new_u; du = new_du; phi = new_phi
            if u <= 0.0:
                escaped = 1.0; break

            nr = 1.0 / u
            c = cos(phi); s = sin(phi)
            er0 = c*e0_0 + s*ep0
            er1 = c*e0_1 + s*ep1
            er2 = c*e0_2 + s*ep2
            epr0 = -s*e0_0 + c*ep0
            epr1 = -s*e0_1 + c*ep1
            epr2 = -s*e0_2 + c*ep2
            cp0 = nr*er0; cp1 = nr*er1; cp2 = nr*er2

            old_y = prev_r * (cos(prev_phi)*e0_1 + sin(prev_phi)*ep1)
            new_y = cp1
            crossed = 0.0
            if (old_y > 0.0 and new_y <= 0.0) or (old_y < 0.0 and new_y >= 0.0):
                crossed = 1.0

            if crossed == 1.0:
                den = old_y - new_y
                if abs(den) > 1e-6:
                    hit_t = old_y / den
                    hit_t = clamp(hit_t, 0.0, 1.0)
                    hit_u = prev_u*(1.0-hit_t) + u*hit_t
                    hit_phi = prev_phi*(1.0-hit_t) + phi*hit_t
                    hit_r = 1.0 / max(hit_u, 1e-6)
                    if hit_r >= 6.0*M and hit_r <= 12.0*M:
                        hc = cos(hit_phi); hs = sin(hit_phi)
                        her0 = hc*e0_0 + hs*ep0
                        her1 = hc*e0_1 + hs*ep1
                        her2 = hc*e0_2 + hs*ep2
                        hep0 = -hs*e0_0 + hc*ep0
                        hep1 = -hs*e0_1 + hc*ep1
                        hep2 = -hs*e0_2 + hc*ep2
                        disk_r = hit_r
                        dpx = hit_r * her0
                        dpz = hit_r * her2
                        lf = max(1.0 - 2.0*M/disk_r, 1e-6)
                        hit_du = prev_du*(1.0-hit_t) + du*hit_t
                        nr_comp = -b * hit_du
                        nphi = b * sqrt(lf) / disk_r
                        pad0 = nr_comp*her0 + nphi*hep0
                        pad1 = nr_comp*her1 + nphi*hep1
                        pad2 = nr_comp*her2 + nphi*hep2
                        plen = sqrt(pad0*pad0 + pad1*pad1 + pad2*pad2)
                        pad0 /= plen; pad1 /= plen; pad2 /= plen
                        ddir0 = -pad0; ddir1 = -pad1; ddir2 = -pad2
                        disk_hit = 1.0

    # Background stars
    col_r = 0.005; col_g = 0.005; col_b = 0.015

    if captured == 0.0 and disk_hit == 0.0:
        plen = sqrt(pd0*pd0 + pd1*pd1 + pd2*pd2)
        d0 = pd0/plen; d1 = pd1/plen; d2 = pd2/plen

        s0 = d0*127.1 + d1*311.7 + d2*74.7
        s1 = d0*269.5 + d1*183.3 + d2*246.1
        star_val = fract(sin(s0*12.9898 + s1*78.233)*43758.5453)
        star = step(0.9975, star_val)

        h0 = d0*53.1 + d1*97.3 + d2*63.7
        h1 = d0*34.7 + d1*89.3 + d2*21.5
        star_hue = fract(sin(h0*12.9898 + h1*78.233)*43758.5453)
        col_r += star * (0.55 + 0.45*star_hue)
        col_g += star * (0.55 + 0.45*(1.0 - star_hue))
        col_b += star

        bs0 = d0*89.3 + d1*213.7 + d2*37.1
        bs1 = d0*98.1 + d1*47.3 + d2*156.7
        bright_val = fract(sin(bs0*12.9898 + bs1*78.233)*43758.5453)
        bright = step(0.9995, bright_val) * 1.5
        col_r += bright*0.9; col_g += bright*0.8; col_b += bright

    # Accretion disk – pure linear transfer, no Reinhard
    if disk_hit == 1.0:
        r = disk_r
        v_den = max(r - 2.0*M, 1e-6)
        v_mag = sqrt(M / v_den)
        v_mag = clamp(v_mag, 0.0, 0.999)

        t0 = -dpx / r; t1 = 0.0; t2 = dpz / r
        tl = sqrt(t0*t0 + t1*t1 + t2*t2)
        t0 /= tl; t1 /= tl; t2 /= tl

        vel0 = v_mag * t0; vel1 = v_mag * t1; vel2 = v_mag * t2
        beta_dot_n = vel0*ddir0 + vel1*ddir1 + vel2*ddir2
        gamma_arg = max(1.0 - v_mag*v_mag, 1e-6)
        gamma = 1.0 / sqrt(gamma_arg)
        grav_arg = max(1.0 - 2.0*M/r, 1e-6)
        grav = sqrt(grav_arg)
        den = max(gamma * (1.0 - beta_dot_n), 1e-6)
        g_factor = grav / den
        g_factor = clamp(g_factor, 0.0, 3.0)

        obs_intensity = DISK_LUMINOSITY * (g_factor * g_factor * g_factor)

        # ---- Radial gradient interpolation ----
        t_disk = (r - 6.0) / 10.0        # 0 at inner (6M), 1 at outer (16M)
        t_disk = clamp(t_disk, 0.0, 1.0)

        col_r = (inner_colour_r * (1.0 - t_disk) + outer_colour_r * t_disk) * obs_intensity
        col_g = (inner_colour_g * (1.0 - t_disk) + outer_colour_g * t_disk) * obs_intensity
        col_b = (inner_colour_b * (1.0 - t_disk) + outer_colour_b * t_disk) * obs_intensity

    # Final exposure & clamp
    col_r *= EXPOSURE
    col_g *= EXPOSURE
    col_b *= EXPOSURE

    # Simple clamp to [0,1] – no Reinhard compression.
    col_r = clamp(col_r, 0.0, 1.0)
    col_g = clamp(col_g, 0.0, 1.0)
    col_b = clamp(col_b, 0.0, 1.0)

    color.r = col_r
    color.g = col_g
    color.b = col_b
    color.a = 1.0

# 3. Apply script and main loop
sim.paint(blackhole_gr._script_id)
sim.set_camera_zoom(3)          # override initial zoom
print("Schwarzschild GR Lensing")
print("Controls: Left/Right : orbit yaw, Up/Down : orbit pitch")

# Initial camera values (will be written to scratchpad each frame)
yaw = 0.5
pitch = 1.7
step = 0.01

while not sim.should_close():
    sim.default_input()
    sim.process_input()
    # Read keyboard and update yaw/pitch in Python
    if sim.keyboard.Left.pressed:
        yaw -= step
    if sim.keyboard.Right.pressed:
        yaw += step
    if sim.keyboard.Up.pressed:
        pitch += step
    if sim.keyboard.Down.pressed:
        pitch -= step
    pitch = max(0.01, min(math.pi - 0.01, pitch))

    # Write updated camera parameters to scratchpad (slot 0, indices 0 and 1)
    sim.upload_scratchpad(CAMERA_SCRATCH_ID, [yaw, pitch])
    sim.update(0.016)
    sim.render()