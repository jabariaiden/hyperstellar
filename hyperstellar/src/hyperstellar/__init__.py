# hyperstellar/src/hyperstellar/__init__.py
import os
import sys
import time
import platform
from pathlib import Path

# Import the decorator function from jit.py (module-level)
from .jit import script as _script_func

__version__ = "1.5.0"

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
# Determine if shader cache already exists (to decide whether to print warning)
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
    # Check if there's any .bin file (indicating a previous compilation)
    has_cache = any(cache_dir.glob("*.bin"))

# Print a warning only if no cache exists (first-time compilation)
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

# Import native module (this triggers shader compilation if cache missing)
try:
    sys.modules.pop('stellar', None)
    sys.path.insert(0, str(module_dir))
    import stellar as _stellar_module
    sys.path.pop(0)

    # Expose everything from the native module
    for attr_name in dir(_stellar_module):
        if not attr_name.startswith('__'):
            globals()[attr_name] = getattr(_stellar_module, attr_name)

    # Define __all__ (include script at module level)
    if hasattr(_stellar_module, '__all__'):
        __all__ = _stellar_module.__all__ + ['script']
    else:
        __all__ = ['script']

    # Attach @sim.script decorator to the Simulation class
    def _script_method(self, mode='object', debug=False):
        """
        Decorator that compiles a Python function into a GPU compute shader.

        Use as: @sim.script(mode='paint')
        """
        def decorator(func):
            return _script_func(self, debug=debug, mode=mode)(func)
        return decorator

    Simulation.script = _script_method

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