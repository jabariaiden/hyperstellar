# hyperstellar/src/hyperstellar/__init__.py
import os
import sys
import platform
from pathlib import Path

__version__ = "0.1.24"

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

# Import native module
try:
    sys.modules.pop('stellar', None)
    sys.path.insert(0, str(module_dir))
    import stellar as _stellar_module
    sys.path.pop(0)

    for attr_name in dir(_stellar_module):
        if not attr_name.startswith('__'):
            globals()[attr_name] = getattr(_stellar_module, attr_name)

    if hasattr(_stellar_module, '__all__'):
        __all__ = _stellar_module.__all__

    print(f"✓ hyperstellar {__version__} loaded ({system} {arch})")

except ImportError as e:
    raise ImportError(
        f"Failed to load native module: {e}\n"
        f"Module path: {module_path}\n"
        f"Platform: {system} {arch}"
    )
except Exception as e:
    raise ImportError(f"Unexpected error loading hyperstellar: {e}")