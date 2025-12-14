# hyperstellar/src/hyperstellar/__init__.py
import os
import sys
import platform
from pathlib import Path

__version__ = "0.1.0"

# Platform detection
system = platform.system().lower()
arch = platform.machine().lower()

if system == "windows" and ("amd64" in arch or "x86_64" in arch):
    platform_dir = "windows-x64"
    extension = ".pyd"
    lib_name = "stellar.pyd"
else:
    raise ImportError(f"Unsupported platform: {system} {arch}")

# Get the native module path
module_dir = Path(__file__).parent / "_native" / platform_dir
module_path = module_dir / lib_name

if not module_path.exists():
    raise ImportError(f"Native module not found: {module_path}")

# On Windows, add DLL directory to PATH
if system == "windows":
    dll_dir = str(module_dir)
    
    # DEBUG: Print what we're doing
    print(f"[DEBUG] Adding DLL directory: {dll_dir}")
    
    # CRITICAL FIX: Save the original PATH
    original_path = os.environ.get('PATH', '')
    
    # Method 1: Add to PATH at the BEGINNING (highest priority)
    # This must be done BEFORE any attempt to import
    path_sep = ';'
    os.environ['PATH'] = dll_dir + path_sep + original_path
    print(f"[DEBUG] Added to PATH: {dll_dir}")
    
    # Method 2: Use AddDllDirectory for explicit loading
    try:
        import ctypes
        # Clear any previous DLL directory cache
        os.add_dll_directory(dll_dir)
        print(f"[DEBUG] Used os.add_dll_directory")
    except Exception as add_dll_error:
        print(f"[DEBUG] os.add_dll_directory failed: {add_dll_error}")

# Try to import the module
try:
    # First, clear stellar from sys.modules if it exists
    sys.modules.pop('stellar', None)
    
    # CRITICAL: Add the module directory to sys.path temporarily
    sys.path.insert(0, str(module_dir))
    
    # Import the module
    import stellar as _stellar_module
    
    # Remove from sys.path
    sys.path.pop(0)
    
    # Copy ALL public attributes to our module
    for attr_name in dir(_stellar_module):
        if not attr_name.startswith('__'):
            globals()[attr_name] = getattr(_stellar_module, attr_name)
    
    # Also copy __all__ if it exists
    if hasattr(_stellar_module, '__all__'):
        __all__ = _stellar_module.__all__
    
    print(f"✓ hyperstellar loaded: {system} {arch}")
    
except ImportError as e:
    # Provide detailed debug info
    dll_files = list(module_dir.glob('*.dll'))
    raise ImportError(
        f"Failed to load native module: {e}\n"
        f"Module path: {module_path}\n"
        f"DLL directory: {module_dir}\n"
        f"DLLs available: {[dll.name for dll in dll_files]}\n"
        f"Current PATH: {os.environ.get('PATH', '')[:200]}..."
    )
except Exception as e:
    raise ImportError(f"Unexpected error loading module: {e}")