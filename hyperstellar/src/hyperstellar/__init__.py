# hyperstellar/src/hyperstellar/__init__.py
import os
import sys
import time
import platform
from pathlib import Path
from typing import List, Dict, Any, Iterator, Optional, Union

# Import the decorator function from jit.py (module-level)
from .jit import script as _script_func

__version__ = "1.5.6"

# Platform detection
system = platform.system().lower()
arch = platform.machine().lower()

if system == "windows" and ("amd64" in arch or "x86_64" in arch):
    platform_dir = "windows-x64"
    lib_name = "stellar.pyd"
elif system == "linux" and ("x86_64" in arch or "amd64" in arch):
    platform_dir = "linux-x64"
    lib_name = "stellar.so"
elif system == "darwin":
    platform_dir = "macos-x64"
    lib_name = "stellar.so"
else:
    raise ImportError(f"Unsupported platform: {system} {arch}")

# Native module path
module_dir = Path(__file__).parent / "_native" / platform_dir
module_path = module_dir / lib_name

if not module_path.exists():
    raise ImportError(
        f"Native module not found: {module_path}\n"
        f"Platform detected: {system} {arch}\n"
        f"Expected: {platform_dir}/{lib_name}"
    )

# Windows: help loader find DLL dependencies
if system == "windows":
    dll_dir = str(module_dir)
    os.environ['PATH'] = dll_dir + ';' + os.environ.get('PATH', '')
    try:
        os.add_dll_directory(dll_dir)
    except Exception:
        pass

# ----------------------------------------------------------------------
# Determine if shader cache already exists
# ----------------------------------------------------------------------
cache_dir = None
if system == "windows":
    local_appdata = os.environ.get("LOCALAPPDATA")
    if local_appdata:
        cache_dir = Path(local_appdata) / "hyperstellar" / "cache"
else:  # Linux/macOS
    home = os.environ.get("HOME")
    if home:
        cache_dir = Path(home) / ".cache" / "hyperstellar"

has_cache = False
if cache_dir and cache_dir.exists():
    has_cache = any(cache_dir.glob("*.bin"))

if not has_cache:
    sys.stderr.write(
        f"hyperstellar {__version__}: First-time shader compilation may take 30-60 seconds on older GPUs.\n"
        f"         Subsequent runs will be instant (binary cache).\n"
        f"Compiling... "
    )
    sys.stderr.flush()
else:
    sys.stderr.write(f"hyperstellar {__version__}: Loading cached shaders... ")
    sys.stderr.flush()

start_time = time.perf_counter()

# Import native module
try:
    sys.modules.pop('stellar', None)
    sys.path.insert(0, str(module_dir))
    import stellar as _stellar_module
    sys.path.pop(0)

    # Expose everything from the native module
    for attr_name in dir(_stellar_module):
        if not attr_name.startswith('__'):
            globals()[attr_name] = getattr(_stellar_module, attr_name)

    # Define __all__
    if hasattr(_stellar_module, '__all__'):
        __all__ = _stellar_module.__all__ + ['script', 'EquationPresets']
    else:
        __all__ = ['script', 'EquationPresets']

    # Attach @sim.script decorator to the Simulation class
    def _script_method(self, mode='object', debug=False):
        def decorator(func):
            return _script_func(self, debug=debug, mode=mode)(func)
        return decorator

    Simulation.script = _script_method

    # =====================================================================
    # ITERATION PROTOCOL
    # =====================================================================
    def _sim_iter(self):
        for i in range(self.object_count()):
            yield self.get_object(i)

    Simulation.__iter__ = _sim_iter
    Simulation.__len__ = lambda self: self.object_count()

    # =====================================================================
    # CONTEXT MANAGER
    # =====================================================================
    def _sim_enter(self):
        return self

    def _sim_exit(self, exc_type, exc_val, exc_tb):
        self.cleanup()
        return False

    Simulation.__enter__ = _sim_enter
    Simulation.__exit__ = _sim_exit

    # =====================================================================
    # DUPLICATE_OBJECT (uses native get_all_objects, not overwritten)
    # =====================================================================
    def _duplicate_object(self, obj: Union[int, 'ObjectHandle'], **overrides):
        """
        Duplicate an existing object with optional property overrides.

        Args:
            obj: Object ID (raw index or ObjectHandle)
            **overrides: Any property to override (x, y, vx, vy, mass, etc.)

        Returns:
            ObjectHandle: Handle to the new object
        """
        state = self.get_object(obj)   # accepts both raw int and handle
        kwargs = {
            'x': state.x, 'y': state.y,
            'vx': state.vx, 'vy': state.vy,
            'mass': state.mass, 'charge': state.charge,
            'rotation': state.rotation,
            'angular_velocity': state.angular_velocity,
            'skin': state.skin_type,
            'size': state.radius,
            'width': state.width, 'height': state.height,
            'r': state.r, 'g': state.g, 'b': state.b, 'a': state.a,
            'polygon_sides': state.polygon_sides
        }
        kwargs.update(overrides)
        return self.add_object(**kwargs)

    Simulation.duplicate_object = _duplicate_object

    # =====================================================================
    # EQUATION PRESETS
    # =====================================================================
    class EquationPresets:
        @staticmethod
        def gravity(G=1.0, eps=0.001):
            return f"ax = -{G} * x / (x*x + y*y + {eps})**1.5; ay = -{G} * y / (x*x + y*y + {eps})**1.5"

        @staticmethod
        def spring(k=1.0):
            return f"ax = -{k} * x; ay = -{k} * y"

        @staticmethod
        def damped_spring(k=1.0, damping=0.1):
            return f"ax = -{k} * x - {damping} * vx; ay = -{k} * y - {damping} * vy"

        @staticmethod
        def orbit(G=1.0, eps=0.001):
            # Note: this function does NOT accept a 'target' argument; target is handled in set_equation_named.
            return (f"dx = target.x - x; dy = target.y - y; r2 = dx*dx + dy*dy + {eps}; "
                    f"r = sqrt(r2); ax = {G} * target.mass * dx / (r * r2); ay = {G} * target.mass * dy / (r * r2)")

    globals()['EquationPresets'] = EquationPresets

    # =====================================================================
    # OBJECT TAGS/GROUPS (handle‑only, no raw indices)
    # =====================================================================
    class _ObjectTags:
        """
        Lightweight Python-side object tagging system.
        Only ObjectHandles should be stored – raw indices are not accepted.
        """
        def __init__(self, sim):
            self.sim = sim
            self._tags: Dict[str, List['ObjectHandle']] = {}

        def add(self, tag: str, obj: 'ObjectHandle'):
            if not isinstance(obj, ObjectHandle):
                raise TypeError("Only ObjectHandle can be tagged; use add_object() to get a handle.")
            if tag not in self._tags:
                self._tags[tag] = []
            if obj not in self._tags[tag]:
                self._tags[tag].append(obj)

        def remove(self, tag: str, obj: 'ObjectHandle'):
            if tag in self._tags and obj in self._tags[tag]:
                self._tags[tag].remove(obj)
                if not self._tags[tag]:
                    del self._tags[tag]

        def get(self, tag: str) -> List['ObjectHandle']:
            return self._tags.get(tag, []).copy()

        def clear(self, tag: str = None):
            if tag is None:
                self._tags.clear()
            elif tag in self._tags:
                del self._tags[tag]

        def __contains__(self, tag: str) -> bool:
            return tag in self._tags

        def __iter__(self):
            return iter(self._tags.items())

        def __repr__(self):
            return f"<ObjectTags tags={list(self._tags.keys())}>"

    def _get_tags(self):
        if not hasattr(self, '_tags_storage'):
            self._tags_storage = _ObjectTags(self)
        return self._tags_storage

    Simulation.tags = property(_get_tags)

    # =====================================================================
    # REMOVE TAGGED
    # =====================================================================
    def _remove_tagged(self, tag: str):
        if tag in self.tags:
            handles = self.tags.get(tag)
            if handles:
                self.remove_objects(handles)
            self.tags.clear(tag)

    Simulation.remove_tagged = _remove_tagged

    # =====================================================================
    # NAMED EQUATION SETTER (with proper target handling for orbit)
    # =====================================================================
    def _set_equation_named(self, obj, preset_name: str, **kwargs):
        """
        Set a physics equation using a named preset.

        Args:
            obj: Object ID or ObjectHandle
            preset_name (str): "gravity", "spring", "damped_spring", or "orbit"
            **kwargs: Parameters to substitute. For orbit, pass `target=other_handle`.
        """
        # Extract target early for orbit, since orbit's function signature doesn't accept it.
        target = kwargs.pop('target', None) if preset_name == 'orbit' else None

        if preset_name == "gravity":
            base = EquationPresets.gravity(**kwargs)
        elif preset_name == "spring":
            base = EquationPresets.spring(**kwargs)
        elif preset_name == "damped_spring":
            base = EquationPresets.damped_spring(**kwargs)
        elif preset_name == "orbit":
            base = EquationPresets.orbit(**kwargs)
            if target is not None:
                # target can be a raw int or a handle; convert to p[index] syntax.
                if hasattr(target, 'slot'):  # ObjectHandle
                    idx = target.slot
                else:
                    idx = int(target)        # raw index
                base = base.replace('target', f'p[{idx}]')
            else:
                raise ValueError("orbit preset requires a 'target' object (pass target=handle)")
        else:
            raise ValueError(f"Unknown preset: {preset_name}. Available: gravity, spring, damped_spring, orbit")

        self.set_equation(obj, base)

    Simulation.set_equation_named = _set_equation_named

except ImportError as e:
    raise ImportError(
        f"Failed to load native module: {e}\n"
        f"Module path: {module_path}\n"
        f"Platform: {system} {arch}"
    )
except Exception as e:
    raise ImportError(f"Unexpected error loading hyperstellar: {e}")

# Print completion time
elapsed = time.perf_counter() - start_time
sys.stderr.write(f" done in {elapsed:.1f}s.\n")
sys.stderr.flush()