"""
Debugging utilities for the JIT system.

Provides logging, shader source dumping, and rich error reporting with line context.
"""

import sys
import difflib
from typing import Optional, List, Set, Tuple

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
    """Print the generated shader source to stderr."""
    if not _DEBUG_ENABLED:
        return
    if title:
        print(f"\n--- {title} ---", file=sys.stderr)
    print(source, file=sys.stderr)
    print("--- end shader ---\n", file=sys.stderr)


def trace_call(func_name: str, *args, **kwargs) -> None:
    """Log a function call with its arguments (for debugging JIT internals)."""
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


# ----------------------------------------------------------------------------
# Rich error reporting with line context and suggestions
# ----------------------------------------------------------------------------

def _get_line_context(source: str, lineno: int, context_lines: int = 2) -> str:
    """Return a few lines of source around the given line number."""
    if lineno is None or not source:
        return ""
    lines = source.splitlines()
    if lineno < 1 or lineno > len(lines):
        return ""
    start = max(0, lineno - context_lines - 1)
    end = min(len(lines), lineno + context_lines)
    context_lines_output = []
    for i in range(start, end):
        line_num = i + 1
        marker = ">" if line_num == lineno else " "
        context_lines_output.append(f"{line_num:4} {marker} {lines[i]}")
    return "\n".join(context_lines_output)


def report_error(
    msg: str,
    source: str,
    lineno: Optional[int] = None,
    col_offset: Optional[int] = None,
    suggestion: Optional[str] = None,
    context_lines: int = 2,
) -> None:
    """
    Print a formatted error message with source context and an optional suggestion.
    Raises SyntaxError after printing.
    """
    full_msg = f"JIT Error: {msg}"
    if suggestion:
        full_msg += f"\nDid you mean: {suggestion}?"
    if lineno is not None:
        context = _get_line_context(source, lineno, context_lines)
        if context:
            full_msg += f"\n\nIn source:\n{context}"
    error(full_msg)
    raise SyntaxError(full_msg)


def report_warning(
    msg: str,
    source: str,
    lineno: Optional[int] = None,
    col_offset: Optional[int] = None,
    suggestion: Optional[str] = None,
    context_lines: int = 2,
) -> None:
    """Print a formatted warning message with source context."""
    full_msg = f"JIT Warning: {msg}"
    if suggestion:
        full_msg += f"\nDid you mean: {suggestion}?"
    if lineno is not None:
        context = _get_line_context(source, lineno, context_lines)
        if context:
            full_msg += f"\n\nIn source:\n{context}"
    warn(full_msg)


def get_close_matches(word: str, possibilities: Set[str], n: int = 3, cutoff: float = 0.6) -> List[str]:
    """
    Return a list of close matches from a set of possibilities.
    Uses difflib.get_close_matches internally.
    """
    return difflib.get_close_matches(word, list(possibilities), n, cutoff)


def format_error_summary(errors: List[Tuple[str, Optional[int], Optional[int], Optional[str]]],
                         source: str) -> str:
    """
    Format a list of errors (msg, lineno, col_offset, suggestion) into a single
    human-readable string with source context.
    """
    if not errors:
        return ""
    lines = []
    for msg, lineno, col, suggestion in errors:
        line_info = f" at line {lineno}" if lineno is not None else ""
        full_msg = f"{msg}{line_info}"
        if suggestion:
            full_msg += f"\nDid you mean: {suggestion}?"
        if lineno is not None:
            context = _get_line_context(source, lineno, 1)
            if context:
                full_msg += f"\n{context}"
        lines.append(full_msg)
    return "\n\n".join(lines)