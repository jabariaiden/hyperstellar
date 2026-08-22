"""
Debugging utilities for the JIT system.

Provides logging, shader source dumping, and runtime debugging helpers.
"""

import sys
from typing import Optional

# Global debug flag
_DEBUG_ENABLED: bool = False
_LOG_LEVEL: int = 0  # 0=error, 1=warn, 2=info, 3=debug

def enable_debug(flag: bool = True) -> None:
    """Enable or disable debug output."""
    global _DEBUG_ENABLED
    _DEBUG_ENABLED = flag

def set_log_level(level: int) -> None:
    """Set verbosity level (0=error, 1=warn, 2=info, 3=debug)."""
    global _LOG_LEVEL
    _LOG_LEVEL = level

def _log(level: int, msg: str) -> None:
    if not _DEBUG_ENABLED or level > _LOG_LEVEL:
        return
    prefix = ["[ERROR]", "[WARN]", "[INFO]", "[DEBUG]"][level]
    print(f"{prefix} {msg}", file=sys.stderr)

def error(msg: str) -> None:
    """Log an error message."""
    _log(0, msg)

def warn(msg: str) -> None:
    """Log a warning message."""
    _log(1, msg)

def info(msg: str) -> None:
    """Log an info message."""
    _log(2, msg)

def debug(msg: str) -> None:
    """Log a debug message."""
    _log(3, msg)

def log_shader(source: str, title: Optional[str] = None) -> None:
    """
    Print the generated shader source to stderr.

    Args:
        source: The GLSL shader source code.
        title: Optional title to display above the source.
    """
    if not _DEBUG_ENABLED:
        return
    if title:
        print(f"\n--- {title} ---", file=sys.stderr)
    print(source, file=sys.stderr)
    print("--- end shader ---\n", file=sys.stderr)

def trace_call(func_name: str, *args, **kwargs) -> None:
    """
    Log a function call with its arguments (for debugging JIT internals).
    """
    if not _DEBUG_ENABLED:
        return
    args_str = ", ".join(repr(a) for a in args)
    if kwargs:
        kwargs_str = ", ".join(f"{k}={v!r}" for k, v in kwargs.items())
        if args_str:
            args_str += ", " + kwargs_str
        else:
            args_str = kwargs_str
    debug(f"CALL {func_name}({args_str})")