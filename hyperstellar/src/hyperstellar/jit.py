#!/usr/bin/env python3
"""
JIT (Just-In-Time) compilation system for Hyperstellar.

Translates a decorated Python function into a GLSL compute shader.
Supports three modes:
  - 'object' (default): runs per particle, updates Object SSBOs.
  - 'paint': runs per pixel, reads/writes textures (double‑buffered).
  - 'agent': runs over the signal queue (one workgroup per signal).

This module contains:
  - NameChecker: A linter that validates variable and function names, detects
    undefined symbols, unused variables, and unsupported syntax before
    translation.
  - GLSLGenerator: The AST visitor that translates Python to GLSL.
  - script: The public decorator that compiles a Python function.

The pipeline is:
  1. Parse the function source and AST.
  2. Run NameChecker to catch errors (undefined vars, unknown functions, etc.)
  3. If errors exist, abort with a detailed error report.
  4. Otherwise, generate the GLSL.
  5. Wrap the GLSL in the appropriate shader boilerplate (object, paint, or agent).
  6. Register the script with the Simulation.
"""

import ast
import inspect
import textwrap
import difflib
from typing import Any, Dict, Set, Optional, Callable, Union, List, Tuple

# =============================================================================
# IMPORT FROM SIBLING MODULE
# =============================================================================
from . import debug  # use the debug module for logging and error reporting


# =============================================================================
# NameChecker – Linter for JIT scripts
# =============================================================================
class NameChecker(ast.NodeVisitor):
    """
    Linter that checks for:
      - undefined variables (with "did you mean?" suggestions)
      - unused variables (warnings)
      - undefined functions (with "did you mean?" suggestions)
      - unsupported syntax (yield, lambda, async, with, etc.)
      - chained comparisons (a < b < c) – not yet supported
      - invalid function arguments (future)

    It collects all assigned and referenced names, function calls, and then
    validates them against the known built-ins, user-defined functions,
    and imported names.
    """

    def __init__(self, mode: str, debug: bool, source: str, func_node: ast.FunctionDef):
        self.mode = mode
        self.debug = debug
        self.source = source
        self.func_node = func_node

        # --- Sets to be populated during AST traversal ---
        self.assigned: Set[str] = set()          # variables assigned (including parameters)
        self.referenced: Set[str] = set()        # variables referenced
        self.called_functions: Set[str] = set()  # function names called
        self.user_functions: Set[str] = set()    # functions defined inside the script
        self.imported_names: Set[str] = set()    # imported from hyperstellar.glsl
        self.param_names: Set[str] = set()       # function parameters

        # --- Name nodes with their line numbers for error reporting ---
        self.name_nodes: List[Tuple[str, int, int, bool]] = []  # (name, lineno, col, is_func_call)

        # --- Error and warning accumulation ---
        self.errors: List[Tuple[str, int, int, Optional[str]]] = []   # (msg, lineno, col, suggestion)
        self.warnings: List[Tuple[str, int, int, Optional[str]]] = [] # (msg, lineno, col, suggestion)

        # --- Built-in sets ---
        self.builtin_vars = self._get_builtin_vars()
        self.builtin_funcs = self._get_builtin_funcs()

        # --- Run the collector ---
        self._collect()

    def _get_builtin_vars(self) -> Set[str]:
        """Return the set of built-in variable names for the current mode."""
        common = {'num_objects', 'dt', 'time', 'idx', 'group_count', 'i', 'p'}
        if self.mode == 'object':
            return common | {'x', 'y', 'vx', 'vy', 'mass', 'charge', 'theta', 'omega',
                             'ax', 'ay', 'angular', 'color', 'pos', 'vel'}
        elif self.mode == 'paint':
            return common | {'px', 'py', 'prev_r', 'prev_g', 'prev_b', 'prev_a',
                             'color', 't'}
        elif self.mode == 'agent':
            return common | {'signal_object_idx', 'signal_payload'}
        else:
            return common

    def _get_builtin_funcs(self) -> Set[str]:
        """Return the set of built-in function names exposed to JIT scripts."""
        return {
            # Math
            'sqrt', 'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'atan2',
            'sinh', 'cosh', 'tanh', 'asinh', 'acosh', 'atanh',
            'exp', 'log', 'log2', 'exp2', 'pow', 'abs', 'floor', 'ceil',
            'fract', 'sign', 'step', 'smoothstep', 'mix', 'clamp', 'mod',
            'min', 'max', 'dot', 'cross', 'length', 'distance', 'normalize',
            'reflect', 'refract', 'faceforward', 'transpose', 'inverse',
            'determinant', 'outerProduct', 'matrixCompMult',
            'radians', 'degrees', 'saturate', 'lerp', 'fbm', 'noise', 'rand',
            # JIT-specific
            'signal', 'scratchpad_read', 'scratchpad_write',
            'apply_constraints', 'detect_collision', 'resolve_collision',
            # Ray-tracing
            'sphere', 'plane', 'intersect', 'miss_hit', 'closest_hit',
            'look_at', 'camera_ray', 'camera_ray_ndc', 'offset_ray_origin',
            'reflect', 'refract',
            # Complex
            'cAdd', 'cSub', 'cMul', 'cDiv', 'cLog', 'cExp', 'cPow',
            'cSin', 'cCos', 'cTan', 'real', 'imag', 'conj', 'arg',
            # Other
            'int', 'float', 'len', 'sum', 'diff',
            # Paint helpers
            'sample_prev_r', 'sample_prev_g', 'sample_prev_b', 'sample_prev_a',
            'avg_prev_r', 'avg_prev_g', 'avg_prev_b', 'avg_prev_a',
        }

    def _collect(self) -> None:
        """Walk the AST to collect assignments, references, and function calls."""
        # First, collect parameters
        for arg in self.func_node.args.args:
            name = arg.arg
            self.param_names.add(name)
            self.assigned.add(name)

        # Visit the function body (and nested definitions)
        self.visit(self.func_node)

        # After the visit, we have all sets populated.

    # ---- Visitor methods to collect data ----

    def visit_Assign(self, node: ast.Assign) -> None:
        for target in node.targets:
            self._add_target(target)
        self.generic_visit(node)

    def visit_AugAssign(self, node: ast.AugAssign) -> None:
        self._add_target(node.target)
        self.generic_visit(node)

    def visit_For(self, node: ast.For) -> None:
        self._add_target(node.target)
        self.generic_visit(node)

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        # Record user-defined function
        self.user_functions.add(node.name)
        # Also collect its parameters
        for arg in node.args.args:
            self.param_names.add(arg.arg)
            self.assigned.add(arg.arg)
        # Visit the body (but don't treat the function definition itself as a call)
        self.generic_visit(node)

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        if node.module == 'hyperstellar.glsl':
            for alias in node.names:
                self.imported_names.add(alias.name)
        # No further traversal needed for imports

    def visit_Import(self, node: ast.Import) -> None:
        # Regular imports are ignored; we don't track modules.
        pass

    def _add_target(self, target: ast.AST) -> None:
        """Recursively add names from assignment targets."""
        if isinstance(target, ast.Name):
            self.assigned.add(target.id)
        elif isinstance(target, (ast.Tuple, ast.List)):
            for elt in target.elts:
                self._add_target(elt)
        # ignore other targets (attributes, subscripts)

    def visit_Name(self, node: ast.Name) -> None:
        # Record the name reference (we'll check if it's a function call later)
        self.name_nodes.append((node.id, node.lineno, node.col_offset, False))
        self.referenced.add(node.id)
        self.generic_visit(node)

    def visit_Call(self, node: ast.Call) -> None:
        # Record the function name if it's a simple Name
        if isinstance(node.func, ast.Name):
            func_name = node.func.id
            self.called_functions.add(func_name)
            # Also record this as a function call context for the Name node
            self.name_nodes.append((func_name, node.lineno, node.col_offset, True))
        # Visit arguments
        self.generic_visit(node)

    # ---- Unsupported syntax detection ----

    def visit_Lambda(self, node: ast.Lambda) -> None:
        self.errors.append(
            ("Lambda expressions are not supported in JIT scripts.", node.lineno, node.col_offset, None)
        )
        self.generic_visit(node)

    def visit_Yield(self, node: ast.Yield) -> None:
        self.errors.append(
            ("Yield statements are not supported in JIT scripts.", node.lineno, node.col_offset, None)
        )
        self.generic_visit(node)

    def visit_With(self, node: ast.With) -> None:
        self.errors.append(
            ("With statements are not supported in JIT scripts.", node.lineno, node.col_offset, None)
        )
        self.generic_visit(node)

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        self.errors.append(
            ("Async functions are not supported in JIT scripts.", node.lineno, node.col_offset, None)
        )
        self.generic_visit(node)

    def visit_Await(self, node: ast.Await) -> None:
        self.errors.append(
            ("Await expressions are not supported in JIT scripts.", node.lineno, node.col_offset, None)
        )
        self.generic_visit(node)

    def visit_Compare(self, node: ast.Compare) -> None:
        if len(node.ops) > 1:
            self.errors.append(
                ("Chained comparisons (e.g., a < b < c) are not supported; use 'and'.", node.lineno, node.col_offset, None)
            )
        self.generic_visit(node)

    # ---- Validation and suggestion engine ----

    def _suggest_variable(self, name: str) -> Optional[str]:
        """Suggest a similar variable name from the known sets."""
        candidates = (self.builtin_vars | self.assigned | self.param_names) - {name}
        matches = debug.get_close_matches(name, candidates, n=1, cutoff=0.7)
        return matches[0] if matches else None

    def _suggest_function(self, name: str) -> Optional[str]:
        """Suggest a similar function name from built-in or user-defined functions."""
        candidates = (self.builtin_funcs | self.user_functions | self.imported_names) - {name}
        matches = debug.get_close_matches(name, candidates, n=1, cutoff=0.7)
        return matches[0] if matches else None

    def check(self) -> Tuple[List[Tuple[str, int, int, Optional[str]]],
                             List[Tuple[str, int, int, Optional[str]]]]:
        """
        Perform all validation checks and return (errors, warnings).
        Errors are fatal; warnings are informational.
        """
        # Clear any previous errors/warnings (but we already collected during visit)
        # We'll now validate the collected data.

        # 1. Check undefined variables (names that are referenced but not assigned/imported/built-in)
        for name, lineno, col, is_call in self.name_nodes:
            if is_call:
                # This name is used as a function call; we check functions separately.
                continue
            if name not in self.assigned and name not in self.builtin_vars and name not in self.param_names:
                suggestion = self._suggest_variable(name)
                self.errors.append(
                    (f"Undefined variable '{name}'", lineno, col, suggestion)
                )

        # 2. Check undefined functions
        for func_name in self.called_functions:
            if (func_name not in self.builtin_funcs and
                func_name not in self.user_functions and
                func_name not in self.imported_names):
                suggestion = self._suggest_function(func_name)
                self.errors.append(
                    (f"Undefined function '{func_name}'", None, None, suggestion)
                )

        # 3. Check unused variables (assigned but never referenced)
        for var in self.assigned:
            if var not in self.referenced and var not in self.builtin_vars:
                if var in self.param_names:
                    # Parameters are not required to be used, but we issue a warning.
                    self.warnings.append(
                        (f"Parameter '{var}' is never used.", None, None, None)
                    )
                else:
                    self.warnings.append(
                        (f"Variable '{var}' is assigned but never used.", None, None, None)
                    )

        # 4. Check for function arguments count? (future)

        # Return collected errors and warnings
        return self.errors, self.warnings


# =============================================================================
# GLSLGenerator – AST to GLSL translator
# =============================================================================
class GLSLGenerator(ast.NodeVisitor):
    """
    Translates a decorated Python function into a GLSL compute shader.
    Supports three modes:
      - 'object' (default): runs per particle, updates Object SSBOs.
      - 'paint': runs per pixel, reads/writes textures (double‑buffered).
      - 'agent': runs over the signal queue (one workgroup per signal).

    Features:
      - All arithmetic, comparisons, logical ops
      - if/else, for/while, continue/break, ternary
      - Tensor literals and indexing (vec/mat)
      - Derivatives, noise, rand (unique seed)
      - Complex numbers (i, real/imag/conj/arg, cAdd, etc.)
      - sum(), min(), max() for vectors and binary scalars
      - pow(), int(), float() mapped to GLSL
      - p.x / p[index].property access
      - len() for vectors/matrices
      - Full physics: constraints (inline and SSBO‑based), collisions, warm starting
      - Inline constraints: spring, distance, boundary, angle (direct code generation)
      - SSBO‑based constraints: apply_constraints() reads pre‑defined constraints
      - Paint mode with pan/zoom compensation
      - Scratchpad read/write (objects read only, agents read/write)
      - Signal queue enqueue (objects) and dequeue (agents)
      - 3D ray‑tracing primitives: sphere, plane, intersect, camera_ray, raymarch, etc.
      - Tuple unpacking: x, y, z = expr
      - Tuple literals as vector constructors: (x, y, z) → vec3(x, y, z)
      - SDF function inlining for raymarch
      - Extended math functions: asin, acos, atan, sinh, cosh, tanh, asinh, acosh, atanh, exp2, log2, inversesqrt, faceforward, outerProduct, matrixCompMult, transpose, inverse, determinant
      - Utility functions: saturate, lerp, fbm, random (alias for rand)
      - elif chains
      - @ operator for matrix multiplication
      - Swizzling with rgba and stpq sets
      - pass statement (no‑op)
      - Function parameters: declare your function with parameters (e.g., def gravity(x, y):) and they are automatically mapped to the object's state.
      - Explicit return: return (ax, ay, angular) or (ax, ay, angular, color) instead of assigning to magic variables.
      - Explicit imports: from hyperstellar.glsl import sin, cos, sqrt, ...  – these are mapped to GLSL built‑ins and are linter‑friendly.
    """

    def __init__(self, debug: bool = False, mode: str = 'object') -> None:
        self.debug = debug
        self.mode = mode
        self.lines: List[str] = []
        self.indent: int = 0
        self.globals: Dict[str, Any] = {}                   # user's global namespace
        self.assigned_vars: Set[str] = set()               # variables that need declaration
        self.var_types: Dict[str, str] = {}                # variable -> GLSL type
        self.complex_vars: Set[str] = set()                # names known to be complex
        self.inline_depth: int = 0
        self.max_inline_depth: int = 10
        self.inlined_functions: Set[str] = set()
        self.rand_call_counter: int = 0                    # unique seed for each rand()
        self.user_handles_collisions: bool = False
        self.user_applies_constraints: bool = False
        self.sdf_functions: Dict[str, Tuple[ast.FunctionDef, List[str]]] = {}
        self.raymarch_functions: Dict[str, str] = {}
        self.has_return: bool = False                      # whether function uses return
        self.return_expr: Optional[ast.AST] = None         # captured return expression
        self.param_names: List[str] = []                   # function parameter names
        self.imported_names: Set[str] = set()              # names imported from hyperstellar.glsl

    def indent_str(self) -> str:
        return "    " * self.indent

    # ------------------------------------------------------------------------
    # Main entry point
    # ------------------------------------------------------------------------
    def generate(self, func: Callable) -> str:
        """
        Parse the given Python function, visit its AST, and produce the
        complete GLSL shader source.
        """
        self.globals = func.__globals__
        self.lines = []
        self.assigned_vars.clear()
        self.var_types.clear()
        self.complex_vars.clear()
        self.inline_depth = 0
        self.inlined_functions.clear()
        self.rand_call_counter = 0
        self.user_handles_collisions = False
        self.user_applies_constraints = False
        self.sdf_functions.clear()
        self.raymarch_functions.clear()
        self.has_return = False
        self.return_expr = None
        self.param_names = []
        self.imported_names.clear()

        # Get source and AST
        source = inspect.getsource(func)
        tree = ast.parse(source)
        if not isinstance(tree.body[0], ast.FunctionDef):
            raise ValueError("Not a function definition")
        func_node = tree.body[0]

        # ---- LINTER STAGE ----
        linter = NameChecker(self.mode, self.debug, source, func_node)
        errors, warnings = linter.check()

        # Report warnings if debug is enabled
        if self.debug and warnings:
            for msg, lineno, col, suggestion in warnings:
                debug.report_warning(msg, source, lineno, col, suggestion)

        # If there are errors, abort with a detailed summary
        if errors:
            error_summary = debug.format_error_summary(errors, source)
            raise SyntaxError(f"JIT compilation failed due to the following errors:\n{error_summary}")

        # ---- Proceed with generation ----
        # Extract parameter names
        self.param_names = [arg.arg for arg in func_node.args.args]

        collector = AssignCollector()
        collector.visit(func_node)

        # Detect if user calls collision or SSBO‑constraint functions
        special_funcs = {'detect_collision', 'resolve_collision', 'apply_constraints'}
        for node in ast.walk(func_node):
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
                if node.func.id == 'apply_constraints':
                    self.user_applies_constraints = True
                elif node.func.id in ('detect_collision', 'resolve_collision'):
                    self.user_handles_collisions = True

        # Check for return statement and capture its expression
        self._scan_for_return(func_node)

        # Predefined variables depend on mode
        if self.mode == 'paint':
            predefined = {'px', 'py', 'prev_r', 'prev_g', 'prev_b', 'prev_a',
                          't', 'color', 'idx', 'num_objects', 'dt', 'time',
                          'group_count', 'i'}
        elif self.mode == 'agent':
            predefined = {'signal_object_idx', 'signal_payload', 'num_objects', 'dt',
                          'time', 'idx', 'group_count', 'i'}
        else:
            predefined = {'x', 'y', 'vx', 'vy', 'mass', 'charge', 'theta', 'omega',
                          'color', 'pos', 'vel', 'num_objects', 'dt', 'time', 'idx',
                          'group_count', 'i', 'ax', 'ay', 'angular'}
        self.assigned_vars = collector.vars - predefined
        if self.mode == 'paint':
            self.assigned_vars.add('color')
        if self.has_return:
            self.assigned_vars.add('ax')
            self.assigned_vars.add('ay')
            self.assigned_vars.add('angular')
            if self.mode == 'paint':
                self.assigned_vars.add('color')

        # Collect SDF functions
        for node in ast.walk(func_node):
            if isinstance(node, ast.FunctionDef) and node.name != func_node.name:
                if len(node.args.args) == 1:
                    self.sdf_functions[node.name] = (node, [arg.arg for arg in node.args.args])

        # Walk the AST (this builds self.lines with the user's code)
        self.visit(func_node)

        # Build prologue to map parameters to built-in variables
        prologue = self._build_parameter_prologue()

        # Combine prologue + user code
        body = "\n".join(self.lines)
        if prologue:
            body = prologue + "\n" + body

        # If we have a return expression, we need to insert the assignments after the user code
        if self.has_return and self.return_expr is not None:
            ret_code = self._process_return(self.return_expr)
            if ret_code:
                body += "\n" + ret_code

        # Delegate shader wrapping to the separate module
        from . import shader
        return shader.wrap_shader(
            mode=self.mode,
            body=body,
            header=self._ray_tracing_header(),
            sdf_defs=self._generate_sdf_defs(),
            assigned_vars=self.assigned_vars,
            var_types=self.var_types,
            user_handles_collisions=self.user_handles_collisions,
            user_applies_constraints=self.user_applies_constraints,
            debug=self.debug
        )

    # ------------------------------------------------------------------------
    # Build parameter mapping prologue
    # ------------------------------------------------------------------------
    def _build_parameter_prologue(self) -> str:
        """
        Generate GLSL code that assigns the function parameters to the
        corresponding object state variables. For example, if the user wrote:
            def gravity(x, y):
                ...
        we will generate:
            float x = pos.x;
            float y = pos.y;
        """
        if not self.param_names:
            return ""

        mapping = {
            'x': 'pos.x', 'y': 'pos.y',
            'vx': 'vel.x', 'vy': 'vel.y',
            'mass': 'mass', 'charge': 'charge',
            'theta': 'theta', 'omega': 'omega',
            'color': 'color',   # vec4
            'pos': 'pos', 'vel': 'vel',
        }

        lines = []
        for pname in self.param_names:
            if pname in mapping:
                expr = mapping[pname]
                if pname == 'color':
                    typ = 'vec4'
                elif pname in ('pos', 'vel'):
                    typ = 'vec2'
                else:
                    typ = 'float'
                lines.append(f"    {typ} {pname} = {expr};")
            else:
                lines.append(f"    // WARNING: parameter '{pname}' is not a known object property; defaulting to 0.0")
                lines.append(f"    float {pname} = 0.0;")
        return "\n".join(lines)

    # ------------------------------------------------------------------------
    # Helper: scan for return statement
    # ------------------------------------------------------------------------
    def _scan_for_return(self, node: ast.FunctionDef) -> None:
        for stmt in node.body:
            if isinstance(stmt, ast.Return):
                self.has_return = True
                self.return_expr = stmt.value
                return
            if isinstance(stmt, (ast.If, ast.For, ast.While)):
                for inner in stmt.body:
                    if isinstance(inner, ast.Return):
                        self.has_return = True
                        self.return_expr = inner.value
                        return

    # ------------------------------------------------------------------------
    # Helper: process return expression and generate assignment code
    # ------------------------------------------------------------------------
    def _process_return(self, node: ast.AST) -> str:
        if not isinstance(node, (ast.Tuple, ast.List)):
            raise SyntaxError("return must be a tuple of (ax, ay, angular) or (ax, ay, angular, color)")
        elems = node.elts
        if not (3 <= len(elems) <= 4):
            raise SyntaxError("return must have 3 or 4 elements: (ax, ay, angular) or (ax, ay, angular, color)")

        ax_glsl = self._expr_to_glsl(elems[0])
        ay_glsl = self._expr_to_glsl(elems[1])
        angular_glsl = self._expr_to_glsl(elems[2])
        lines = []
        lines.append(f"    ax = {ax_glsl};")
        lines.append(f"    ay = {ay_glsl};")
        lines.append(f"    angular = {angular_glsl};")
        if len(elems) == 4:
            color_glsl = self._expr_to_glsl(elems[3])
            lines.append(f"    color = {color_glsl};")
        return "\n".join(lines)

    # ------------------------------------------------------------------------
    # Helper: generate SDF and raymarch definitions
    # ------------------------------------------------------------------------
    def _generate_sdf_defs(self) -> str:
        sdf_defs = ""
        for name, (sdf_node, params) in self.sdf_functions.items():
            param_name = params[0]
            temp_gen = GLSLGenerator(debug=self.debug, mode='object')
            temp_gen.var_types = self.var_types.copy()
            temp_gen.complex_vars = self.complex_vars.copy()
            temp_gen.var_types[param_name] = 'vec3'
            if len(sdf_node.body) == 1 and isinstance(sdf_node.body[0], ast.Return):
                ret_expr = temp_gen._expr_to_glsl(sdf_node.body[0].value)
                sdf_defs += f"float {name}(vec3 {param_name}) {{ return {ret_expr}; }}\n"
            else:
                temp_gen.lines = []
                temp_gen.indent = 0
                for stmt in sdf_node.body:
                    temp_gen.visit(stmt)
                body_lines = "\n".join(temp_gen.lines)
                sdf_defs += f"float {name}(vec3 {param_name}) {{\n{body_lines}\n}}\n"

        # Generate specialized raymarch functions
        raymarch_defs = ""
        for sdf_name in self.sdf_functions.keys():
            func_name = f"raymarch_{sdf_name}"
            raymarch_defs += f"""
Hit {func_name}(Ray r, float t_min, float t_max, int steps, float eps) {{
    Hit h; h.hit = false; h.t = 1e10;
    float t = t_min;
    for (int i = 0; i < steps; i++) {{
        vec3 p = r.origin + t * r.direction;
        float d = {sdf_name}(p);
        if (d < eps) {{
            h.hit = true; h.t = t; h.point = p;
            float e = max(eps * 10.0, 1e-5);
            vec3 n = normalize(vec3(
                {sdf_name}(p + vec3(e,0,0)) - {sdf_name}(p - vec3(e,0,0)),
                {sdf_name}(p + vec3(0,e,0)) - {sdf_name}(p - vec3(0,e,0)),
                {sdf_name}(p + vec3(0,0,e)) - {sdf_name}(p - vec3(0,0,e))
            ));
            h.normal = n;
            h.uv = vec2(0.0);
            h.material = 0;
            break;
        }}
        t += d;
        if (t > t_max) break;
    }}
    return h;
}}
"""
        return sdf_defs + raymarch_defs

    # ------------------------------------------------------------------------
    # Ray‑tracing GLSL header (extended with additional utilities)
    # ------------------------------------------------------------------------
    def _ray_tracing_header(self) -> str:
        return """
// ---- 3D Ray‑tracing primitives ----
struct Ray { vec3 origin; vec3 direction; };
struct Hit { bool hit; float t; vec3 point; vec3 normal; vec2 uv; int material; };
struct CameraBasis { vec3 right; vec3 up; vec3 forward; };

#define OBJECT_TYPE_SPHERE 0
#define OBJECT_TYPE_PLANE  1
struct GeoObject { int type; vec4 data; };

GeoObject sphere(vec3 center, float radius) {
    GeoObject o; o.type = OBJECT_TYPE_SPHERE; o.data = vec4(center, radius); return o;
}
GeoObject plane(vec3 normal, float d) {
    GeoObject o; o.type = OBJECT_TYPE_PLANE; o.data = vec4(normal, d); return o;
}

Hit intersectSphere(Ray r, vec3 center, float radius) {
    vec3 oc = r.origin - center;
    float a = dot(r.direction, r.direction);
    float b = 2.0 * dot(oc, r.direction);
    float c = dot(oc, oc) - radius*radius;
    float disc = b*b - 4.0*a*c;
    Hit h; h.hit = false; h.t = 1e10; h.point = vec3(0.0); h.normal = vec3(0.0); h.uv = vec2(0.0); h.material = 0;
    if (disc > 0.0) {
        float t = (-b - sqrt(disc)) / (2.0 * a);
        if (t > 0.0) {
            h.hit = true; h.t = t;
            h.point = r.origin + t * r.direction;
            h.normal = normalize(h.point - center);
            h.uv = vec2(0.0);
            h.material = 0;
        }
    }
    return h;
}

Hit intersectPlane(Ray r, vec3 normal, float d) {
    Hit h; h.hit = false; h.t = 1e10; h.point = vec3(0.0); h.normal = vec3(0.0); h.uv = vec2(0.0); h.material = 0;
    float denom = dot(r.direction, normal);
    if (abs(denom) > 1e-6) {
        float t = -(dot(r.origin, normal) + d) / denom;
        if (t > 0.0) {
            h.hit = true; h.t = t;
            h.point = r.origin + t * r.direction;
            h.normal = normal;
            h.uv = vec2(0.0);
            h.material = 0;
        }
    }
    return h;
}

Hit intersect(Ray r, GeoObject obj) {
    if (obj.type == OBJECT_TYPE_SPHERE) {
        return intersectSphere(r, obj.data.xyz, obj.data.w);
    } else if (obj.type == OBJECT_TYPE_PLANE) {
        return intersectPlane(r, obj.data.xyz, obj.data.w);
    }
    Hit miss; miss.hit = false; miss.t = 1e10; return miss;
}

Hit miss_hit() {
    Hit h; h.hit = false; h.t = 1e10; h.point = vec3(0.0); h.normal = vec3(0.0); h.uv = vec2(0.0); h.material = 0;
    return h;
}

Hit closest_hit(Hit a, Hit b) {
    if (!a.hit && !b.hit) return miss_hit();
    if (!a.hit) return b;
    if (!b.hit) return a;
    return (a.t < b.t) ? a : b;
}

CameraBasis look_at(vec3 eye, vec3 target, vec3 up) {
    CameraBasis cb;
    cb.forward = normalize(target - eye);
    cb.right = normalize(cross(cb.forward, up));
    cb.up = cross(cb.right, cb.forward);
    return cb;
}

// ---- Explicit version for custom NDC (jitter, DOF, secondary rays, etc.) ----
Ray camera_ray_ndc(float ndcX, float ndcY, vec3 eye, vec3 target, vec3 up, float fov, float aspect) {
    CameraBasis cb = look_at(eye, target, up);
    float tanFov = tan(radians(fov) * 0.5);
    vec3 dir = normalize(cb.forward + tanFov * (ndcX * aspect * cb.right + ndcY * cb.up));
    Ray r; r.origin = eye; r.direction = dir; return r;
}

// ---- Helper that uses px/py to integrate with 2D camera ----
Ray camera_ray_pxpy(vec3 eye, vec3 target, vec3 up, float fov, float px, float py) {
    vec3 fwd = normalize(target - eye);
    vec3 right = normalize(vec3(fwd.z, 0.0, -fwd.x));
    vec3 up_vec = cross(right, fwd);
    float focal = 1.0 / tan(radians(fov) * 0.5);
    vec3 dir = normalize(px * right + py * up_vec + focal * fwd);
    Ray r; r.origin = eye; r.direction = dir; return r;
}

// Macro forwards to the helper, passing px and py from the calling scope.
#define camera_ray(eye, target, up, fov) camera_ray_pxpy(eye, target, up, fov, px, py)

vec3 offset_ray_origin(vec3 pos, vec3 normal) {
    return pos + normal * 1e-4;
}

vec3 reflect(vec3 v, vec3 n) {
    return v - 2.0 * dot(v, n) * n;
}

vec3 refract(vec3 v, vec3 n, float eta) {
    float cosi = -dot(v, n);
    float cost2 = 1.0 - eta*eta * (1.0 - cosi*cosi);
    if (cost2 < 0.0) return vec3(0.0);
    return eta * v + (eta * cosi - sqrt(cost2)) * n;
}

// ---- Random and noise functions ----
float rand(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}
float smoothNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = rand(i);
    float b = rand(i + vec2(1.0, 0.0));
    float c = rand(i + vec2(0.0, 1.0));
    float d = rand(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float noise(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    float freq = 4.0;
    for (int i = 0; i < 3; ++i) {
        sum += amp * smoothNoise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return sum;
}

// ---- Additional math utilities ----
float saturate(float x) { return clamp(x, 0.0, 1.0); }
vec2 saturate(vec2 x) { return clamp(x, 0.0, 1.0); }
vec3 saturate(vec3 x) { return clamp(x, 0.0, 1.0); }
vec4 saturate(vec4 x) { return clamp(x, 0.0, 1.0); }

float lerp(float a, float b, float t) { return mix(a, b, t); }
vec2 lerp(vec2 a, vec2 b, float t) { return mix(a, b, t); }
vec3 lerp(vec3 a, vec3 b, float t) { return mix(a, b, t); }
vec4 lerp(vec4 a, vec4 b, float t) { return mix(a, b, t); }

float random(vec2 seed) { return rand(seed); }

float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < octaves; i++) {
        value += amp * noise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return value;
}
"""

    # ------------------------------------------------------------------------
    # AST Visitors
    # ------------------------------------------------------------------------
    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        for stmt in node.body:
            self.visit(stmt)

    def visit_Pass(self, node: ast.Pass) -> None:
        # Do nothing; pass is a no-op in GLSL
        pass

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        if node.module == 'hyperstellar.glsl':
            for alias in node.names:
                self.imported_names.add(alias.name)
        # No GLSL code emitted for imports

    def visit_Import(self, node: ast.Import) -> None:
        # Ignore regular imports
        pass

    def visit_Expr(self, node: ast.Expr) -> None:
        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name):
            func_name = node.value.func.id
            if func_name == 'apply_constraints':
                self.lines.append(self.indent_str() + "apply_constraints();")
                return
            if func_name == 'resolve_collision':
                args = [self._expr_to_glsl(a) for a in node.value.args]
                self.lines.append(self.indent_str() + f"resolve_collision({', '.join(args)});")
                return
            if func_name == 'spring':
                self._handle_spring(node.value)
                return
            if func_name == 'distance':
                self._handle_distance(node.value)
                return
            if func_name == 'boundary':
                self._handle_boundary(node.value)
                return
            if func_name == 'angle':
                self._handle_angle(node.value)
                return
        self.generic_visit(node)

    # ---- Inline constraint handlers ----
    def _handle_spring(self, node: ast.Call) -> None:
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        target = self._expr_to_glsl(kwargs.get('target', ast.Constant(value=0)))
        stiffness = self._expr_to_glsl(kwargs.get('stiffness', ast.Constant(value=1.0)))
        damping = self._expr_to_glsl(kwargs.get('damping', ast.Constant(value=0.0)))
        rest = self._expr_to_glsl(kwargs.get('rest_length', ast.Constant(value=1.0)))

        code = f'''
        {{
            int other = int({target});
            if (other >= 0 && other < uNumObjects && other != self) {{
                vec2 otherPos = objectsIn[other].position;
                vec2 otherVel = objectsIn[other].velocity;
                float otherMass = max(EPSILON, objectsIn[other].mass);
                vec2 delta = pos - otherPos;
                float dist = length(delta);
                if (dist > EPSILON) {{
                    vec2 dir = delta / dist;
                    float relVel = dot(vel - otherVel, dir);
                    float forceMag = {stiffness} * (dist - {rest}) + {damping} * relVel;
                    vec2 force = -dir * forceMag;
                    ax += force.x / mass;
                    ay += force.y / mass;
                }}
            }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    def _handle_distance(self, node: ast.Call) -> None:
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        target = self._expr_to_glsl(kwargs.get('target', ast.Constant(value=0)))
        length = self._expr_to_glsl(kwargs.get('length', ast.Constant(value=1.0)))
        beta = 0.2
        gamma = 0.0

        code = f'''
        {{
            int other = int({target});
            if (other >= 0 && other < uNumObjects && other != self) {{
                vec2 otherPos = objectsIn[other].position;
                vec2 otherVel = objectsIn[other].velocity;
                float otherMass = max(EPSILON, objectsIn[other].mass);
                vec2 delta = pos - otherPos;
                float dist = length(delta);
                if (dist > EPSILON) {{
                    vec2 n = delta / dist;
                    float C = dist - {length};
                    float vn = dot(vel - otherVel, n);
                    float invMassSum = (1.0 / mass) + (1.0 / otherMass);
                    float lambda = -(vn + {beta} * C / uDt) / (invMassSum + {gamma});
                    vec2 impulse = lambda * n;
                    vel += impulse / mass;
                    pos += n * ({beta} * C);
                }}
            }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    def _handle_boundary(self, node: ast.Call) -> None:
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        min_x = self._expr_to_glsl(kwargs.get('min_x', ast.Constant(value=-1e10)))
        max_x = self._expr_to_glsl(kwargs.get('max_x', ast.Constant(value=1e10)))
        min_y = self._expr_to_glsl(kwargs.get('min_y', ast.Constant(value=-1e10)))
        max_y = self._expr_to_glsl(kwargs.get('max_y', ast.Constant(value=1e10)))
        elasticity = 0.7
        friction = 0.95

        code = f'''
        {{
            const float elasticity = {elasticity};
            const float friction = {friction};
            if (pos.x < {min_x}) {{ pos.x = {min_x}; vel.x = abs(vel.x) * elasticity; vel.y *= friction; }}
            else if (pos.x > {max_x}) {{ pos.x = {max_x}; vel.x = -abs(vel.x) * elasticity; vel.y *= friction; }}
            if (pos.y < {min_y}) {{ pos.y = {min_y}; vel.y = abs(vel.y) * elasticity; vel.x *= friction; }}
            else if (pos.y > {max_y}) {{ pos.y = {max_y}; vel.y = -abs(vel.y) * elasticity; vel.x *= friction; }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    def _handle_angle(self, node: ast.Call) -> None:
        kwargs = {kw.arg: kw.value for kw in node.keywords}
        target1 = self._expr_to_glsl(kwargs.get('target1', ast.Constant(value=0)))
        target2 = self._expr_to_glsl(kwargs.get('target2', ast.Constant(value=0)))
        target_angle = self._expr_to_glsl(kwargs.get('angle', ast.Constant(value=0.0)))
        beta = 0.2
        gamma = 0.0

        code = f'''
        {{
            int other1 = int({target1});
            int other2 = int({target2});
            if (other1 >= 0 && other1 < uNumObjects && other1 != self &&
                other2 >= 0 && other2 < uNumObjects && other2 != self &&
                other1 != other2) {{

                vec2 B = objectsIn[other1].position;
                vec2 C = objectsIn[other2].position;
                vec2 r1 = B - pos;
                vec2 r2 = C - pos;
                float dotProd = dot(r1, r2);
                float crossProd = r1.x*r2.y - r1.y*r2.x;
                float currentAngle = atan(crossProd, dotProd);

                // Error
                float C_err = currentAngle - {target_angle};

                // Compute gradient of angle w.r.t. pos (self) using finite differences
                float eps = EPSILON;
                vec2 pos_xp = pos + vec2(eps, 0.0);
                vec2 pos_xm = pos - vec2(eps, 0.0);
                vec2 pos_yp = pos + vec2(0.0, eps);
                vec2 pos_ym = pos - vec2(0.0, eps);

                float angle_xp = atan( (B-pos_xp).x*(C-pos_xp).y - (B-pos_xp).y*(C-pos_xp).x,
                                       dot(B-pos_xp, C-pos_xp) );
                float angle_xm = atan( (B-pos_xm).x*(C-pos_xm).y - (B-pos_xm).y*(C-pos_xm).x,
                                       dot(B-pos_xm, C-pos_xm) );
                float angle_yp = atan( (B-pos_yp).x*(C-pos_yp).y - (B-pos_yp).y*(C-pos_yp).x,
                                       dot(B-pos_yp, C-pos_yp) );
                float angle_ym = atan( (B-pos_ym).x*(C-pos_ym).y - (B-pos_ym).y*(C-pos_ym).x,
                                       dot(B-pos_ym, C-pos_ym) );

                vec2 g = vec2( (angle_xp - angle_xm) / (2.0*eps),
                               (angle_yp - angle_ym) / (2.0*eps) );

                float g2 = dot(g, g);
                if (g2 > 1e-10) {{
                    float vn = dot(vel, g);
                    float lambda = -(vn + {beta} * C_err / uDt) / (g2 / mass + {gamma});
                    vel += lambda * g / mass;
                    float posCorr = -{beta} * C_err / g2;
                    pos += posCorr * g;
                }}
            }}
        }}
        '''
        self.lines.append(self.indent_str() + code.strip())

    # ---- Assign (with tuple unpacking support) ----
    def visit_Assign(self, node: ast.Assign) -> None:
        if len(node.targets) != 1:
            raise SyntaxError("Only single target assignments supported")
        target = node.targets[0]
        expr_str = self._expr_to_glsl(node.value)

        if isinstance(target, ast.Name):
            varname = target.id
            typ = self._infer_expr_type(node.value)
            if typ:
                self.var_types[varname] = typ
            if self._is_complex_expr(node.value):
                self.complex_vars.add(varname)
                if not typ or typ == "float":
                    self.var_types[varname] = "vec2"
            self.lines.append(self.indent_str() + f"{varname} = {expr_str};")
        elif isinstance(target, ast.Tuple):
            rhs_type = self._infer_expr_type(node.value)
            if rhs_type and rhs_type.startswith('vec'):
                comps = ['x', 'y', 'z', 'w']
                for i, elt in enumerate(target.elts):
                    if isinstance(elt, ast.Name):
                        var = elt.id
                        self.lines.append(self.indent_str() + f"{var} = {expr_str}.{comps[i]};")
                    else:
                        raise SyntaxError("Unpacking target must be simple names")
            elif isinstance(node.value, ast.Tuple):
                if len(target.elts) != len(node.value.elts):
                    raise SyntaxError("Tuple unpacking length mismatch")
                for lhs, rhs in zip(target.elts, node.value.elts):
                    lhs_name = lhs.id if isinstance(lhs, ast.Name) else None
                    if lhs_name is None:
                        raise SyntaxError("Unpacking target must be simple names")
                    rhs_expr = self._expr_to_glsl(rhs)
                    self.lines.append(self.indent_str() + f"{lhs_name} = {rhs_expr};")
            else:
                raise SyntaxError("Unsupported RHS for tuple unpacking")
        elif isinstance(target, ast.Attribute):
            lvalue = self._visit_attribute(target)
            self.lines.append(self.indent_str() + f"{lvalue} = {expr_str};")
        else:
            raise SyntaxError("Unsupported assignment target")

    def visit_AugAssign(self, node: ast.AugAssign) -> None:
        if isinstance(node.target, ast.Name):
            target = node.target.id
            op = self._aug_op(node.op)
            expr = self._expr_to_glsl(node.value)
            if self._is_complex_expr(node.value):
                self.complex_vars.add(target)
                if self.var_types.get(target) in (None, "float"):
                    self.var_types[target] = "vec2"
            self.lines.append(self.indent_str() + f"{target} {op}= {expr};")
        else:
            raise SyntaxError("Only simple augmented assignments")

    # ---- If statement with elif chain support ----
    def visit_If(self, node: ast.If) -> None:
        def emit_if_chain(if_node: ast.If) -> None:
            cond = self._expr_to_glsl(if_node.test)
            self.lines.append(self.indent_str() + f"if ({cond}) {{")
            self.indent += 1
            for stmt in if_node.body:
                self.visit(stmt)
            self.indent -= 1
            if if_node.orelse:
                if len(if_node.orelse) == 1 and isinstance(if_node.orelse[0], ast.If):
                    self.lines.append(self.indent_str() + "} else ")
                    emit_if_chain(if_node.orelse[0])
                else:
                    self.lines.append(self.indent_str() + "} else {")
                    self.indent += 1
                    for stmt in if_node.orelse:
                        self.visit(stmt)
                    self.indent -= 1
                    self.lines.append(self.indent_str() + "}")
            else:
                self.lines.append(self.indent_str() + "}")

        emit_if_chain(node)

    def visit_For(self, node: ast.For) -> None:
        if not isinstance(node.iter, ast.Call) or not isinstance(node.iter.func, ast.Name) or node.iter.func.id != 'range':
            raise SyntaxError("Only 'for i in range(...)' supported")
        args = node.iter.args
        if not (1 <= len(args) <= 3):
            raise SyntaxError("range() requires 1 to 3 arguments")
        var = node.target.id if isinstance(node.target, ast.Name) else "_i"

        start = self._expr_to_glsl(args[0]) if len(args) >= 1 else "0"
        stop = self._expr_to_glsl(args[1]) if len(args) >= 2 else self._expr_to_glsl(args[0])
        step = self._expr_to_glsl(args[2]) if len(args) == 3 else "1"

        if len(args) == 1:
            loop = f"for (int {var} = 0; {var} < int({stop}); {var}++)"
        elif len(args) == 2:
            loop = f"for (int {var} = int({start}); {var} < int({stop}); {var}++)"
        else:
            loop = f"for (int {var} = int({start}); {var} < int({stop}); {var} += int({step}))"

        self.lines.append(self.indent_str() + loop + " {")
        self.indent += 1
        for stmt in node.body:
            self.visit(stmt)
        self.indent -= 1
        self.lines.append(self.indent_str() + "}")

    def visit_While(self, node: ast.While) -> None:
        cond = self._expr_to_glsl(node.test)
        self.lines.append(self.indent_str() + f"while ({cond}) {{")
        self.indent += 1
        for stmt in node.body:
            self.visit(stmt)
        self.indent -= 1
        self.lines.append(self.indent_str() + "}")

    def visit_Continue(self, node: ast.Continue) -> None:
        self.lines.append(self.indent_str() + "continue;")

    def visit_Break(self, node: ast.Break) -> None:
        self.lines.append(self.indent_str() + "break;")

    def visit_IfExp(self, node: ast.IfExp) -> str:
        cond = self._expr_to_glsl(node.test)
        body = self._expr_to_glsl(node.body)
        orelse = self._expr_to_glsl(node.orelse)
        return f"(({cond}) ? ({body}) : ({orelse}))"

    def visit_List(self, node: ast.List) -> str:
        rank = self._list_rank(node)
        if rank == 1:
            elems = [self._expr_to_glsl(e) for e in node.elts]
            n = len(elems)
            if n <= 4:
                return f"vec{n}({', '.join(elems)})"
            else:
                return f"float[{n}]({', '.join(elems)})"
        elif rank == 2:
            rows = node.elts
            if not all(isinstance(r, ast.List) for r in rows):
                raise SyntaxError("Invalid matrix literal: rows must be lists")
            n_rows = len(rows)
            n_cols = len(rows[0].elts)
            elems = []
            for row in rows:
                if len(row.elts) != n_cols:
                    raise SyntaxError("Inconsistent matrix row lengths")
                for e in row.elts:
                    elems.append(self._expr_to_glsl(e))
            if n_rows == n_cols and n_rows in (2,3,4):
                return f"mat{n_rows}({', '.join(elems)})"
            else:
                raise NotImplementedError("Only square 2x2, 3x3, and 4x4 matrices are supported")
        else:
            raise NotImplementedError("Tensor rank > 2 not supported")

    def _list_rank(self, node: ast.List) -> int:
        if not isinstance(node, ast.List):
            return 0
        if not node.elts:
            return 1
        first = node.elts[0]
        if isinstance(first, ast.List):
            return 1 + self._list_rank(first)
        return 1

    def _infer_list_type(self, node: ast.List) -> Optional[str]:
        rank = self._list_rank(node)
        if rank == 1:
            n = len(node.elts)
            if n <= 4:
                return f"vec{n}"
            else:
                return "float[]"
        elif rank == 2:
            rows = node.elts
            if not rows:
                return None
            n_rows = len(rows)
            n_cols = len(rows[0].elts) if isinstance(rows[0], ast.List) else 0
            if n_rows == n_cols and n_rows in (2,3,4):
                return f"mat{n_rows}"
        return None

    def _is_complex_expr(self, node: ast.AST) -> bool:
        if isinstance(node, ast.Constant):
            return isinstance(node.value, complex)
        if isinstance(node, ast.Name):
            return node.id in self.complex_vars
        if isinstance(node, ast.Call):
            func_name = node.func.id if isinstance(node.func, ast.Name) else None
            if func_name in ('cAdd', 'cSub', 'cMul', 'cDiv', 'cLog', 'cExp',
                             'cPow', 'cSin', 'cCos', 'cTan', 'conj'):
                return True
            return False
        if isinstance(node, ast.BinOp):
            return self._is_complex_expr(node.left) or self._is_complex_expr(node.right)
        if isinstance(node, ast.UnaryOp):
            return self._is_complex_expr(node.operand)
        if isinstance(node, ast.Attribute):
            return False
        return False

    # ---- Type inference ----
    def _infer_expr_type(self, node: ast.AST) -> Optional[str]:
        if isinstance(node, ast.Name):
            if node.id in self.imported_names:
                return None
            return self.var_types.get(node.id)
        if isinstance(node, ast.Constant):
            if isinstance(node.value, (int, float)):
                return "float"
            if isinstance(node.value, complex):
                return "vec2"
            return None
        if isinstance(node, ast.List):
            return self._infer_list_type(node)
        if isinstance(node, ast.Tuple):
            n = len(node.elts)
            if n in (2, 3, 4):
                return f"vec{n}"
            return None
        if isinstance(node, ast.BinOp):
            left_type = self._infer_expr_type(node.left)
            right_type = self._infer_expr_type(node.right)
            if left_type and right_type and left_type == right_type:
                return left_type
            if left_type and left_type.startswith('mat') and right_type and right_type.startswith('vec'):
                size = int(left_type[3])
                if right_type == f"vec{size}":
                    return f"vec{size}"
            if left_type and left_type.startswith('vec') and right_type and right_type.startswith('mat'):
                size = int(right_type[3])
                if left_type == f"vec{size}":
                    return f"vec{size}"
            if left_type == "float" and right_type and right_type.startswith('vec'):
                return right_type
            if right_type == "float" and left_type and left_type.startswith('vec'):
                return left_type
            if left_type == "float" and right_type and right_type.startswith('mat'):
                return right_type
            if right_type == "float" and left_type and left_type.startswith('mat'):
                return left_type
            if isinstance(node.op, ast.MatMult):
                if left_type and right_type:
                    if left_type.startswith('mat') and right_type.startswith('mat'):
                        return left_type
                    if left_type.startswith('mat') and right_type.startswith('vec'):
                        size = int(left_type[3])
                        if right_type == f"vec{size}":
                            return f"vec{size}"
            if self._is_complex_expr(node):
                return "vec2"
            return None
        if isinstance(node, ast.UnaryOp):
            return self._infer_expr_type(node.operand)
        if isinstance(node, ast.Call):
            func_name = node.func.id if isinstance(node.func, ast.Name) else None
            if not func_name:
                return None
            # Ray-tracing
            if func_name in ('sphere', 'plane'):
                return "GeoObject"
            if func_name in ('intersect', 'miss_hit', 'closest_hit', 'raymarch'):
                return "Hit"
            if func_name == 'look_at':
                return "CameraBasis"
            if func_name == 'camera_ray' or func_name == 'camera_ray_ndc':
                return "Ray"
            if func_name in ('offset_ray_origin', 'reflect', 'refract'):
                return "vec3"
            if func_name in ('vec2', 'vec3', 'vec4'):
                return func_name
            # Unary functions that return same type as argument
            unary_return_same = {'normalize', 'reflect', 'abs', 'sign', 'floor', 'ceil', 'frac', 'fract',
                                 'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'sinh', 'cosh', 'tanh',
                                 'asinh', 'acosh', 'atanh', 'exp', 'exp2', 'log', 'log2', 'sqrt', 'inversesqrt',
                                 'step', 'clamp', 'saturate', 'lerp', 'mix', 'smoothstep'}
            if func_name in unary_return_same:
                if len(node.args) >= 1:
                    arg_type = self._infer_expr_type(node.args[0])
                    if arg_type and arg_type.startswith('vec'):
                        return arg_type
                return "float"
            if func_name == 'cross':
                return "vec3"
            if func_name == 'mix':
                if len(node.args) >= 3:
                    t1 = self._infer_expr_type(node.args[0])
                    t2 = self._infer_expr_type(node.args[1])
                    if t1 and t2 and t1 == t2:
                        return t1
                    if t1 and t1.startswith('vec'):
                        return t1
                    if t2 and t2.startswith('vec'):
                        return t2
                return "float"
            if func_name == 'smoothstep':
                if len(node.args) >= 3:
                    arg_type = self._infer_expr_type(node.args[2])
                    if arg_type and arg_type.startswith('vec'):
                        return arg_type
                return "float"
            if func_name in ('distance', 'length', 'dot', 'determinant', 'fbm', 'random', 'rand',
                             'real', 'imag', 'arg', 'sum', 'len'):
                return "float"
            if func_name in ('cAdd', 'cSub', 'cMul', 'cDiv', 'cLog', 'cExp',
                             'cPow', 'cSin', 'cCos', 'cTan', 'conj'):
                return "vec2"
            # Matrix functions
            if func_name == 'outerProduct':
                if len(node.args) >= 2:
                    t1 = self._infer_expr_type(node.args[0])
                    t2 = self._infer_expr_type(node.args[1])
                    if t1 and t1.startswith('vec') and t2 and t2.startswith('vec'):
                        n = int(t1[3])
                        m = int(t2[3])
                        return f"mat{n}x{m}"
                return "mat2"
            if func_name == 'matrixCompMult':
                if len(node.args) >= 2:
                    t = self._infer_expr_type(node.args[0])
                    if t and t.startswith('mat'):
                        return t
                return "mat2"
            if func_name == 'transpose':
                if len(node.args) >= 1:
                    t = self._infer_expr_type(node.args[0])
                    if t and t.startswith('mat'):
                        return t
                return "mat2"
            if func_name == 'inverse':
                if len(node.args) >= 1:
                    t = self._infer_expr_type(node.args[0])
                    if t and t.startswith('mat'):
                        return t
                return "mat2"
            if func_name == 'faceforward':
                if len(node.args) >= 3:
                    t = self._infer_expr_type(node.args[0])
                    if t and t.startswith('vec'):
                        return t
                return "vec3"
            if func_name in ('dot', 'length', 'distance', 'determinant'):
                return 'float'
            if func_name in ('max', 'min', 'clamp', 'mod', 'pow', 'sqrt', 'abs', 'sign', 'floor', 'ceil', 'fract', 'step'):
                if len(node.args) >= 1:
                    arg_type = self._infer_expr_type(node.args[0])
                    if arg_type and arg_type.startswith('vec'):
                        return arg_type
                return 'float'
            return None
        if isinstance(node, ast.Subscript):
            value_type = self._infer_expr_type(node.value)
            if value_type and value_type.startswith('vec'):
                return "float"
            if value_type and value_type.startswith('mat'):
                size = int(value_type[3])
                return f"vec{size}"
            return None
        if isinstance(node, ast.Attribute):
            base_type = self._infer_expr_type(node.value)
            if base_type:
                field_type_map = {
                    'Ray': {'origin': 'vec3', 'direction': 'vec3'},
                    'Hit': {'point': 'vec3', 'normal': 'vec3', 'uv': 'vec2', 't': 'float', 'hit': 'bool', 'material': 'int'},
                    'GeoObject': {'data': 'vec4', 'type': 'int'},
                    'CameraBasis': {'right': 'vec3', 'up': 'vec3', 'forward': 'vec3'},
                }
                if base_type in field_type_map and node.attr in field_type_map[base_type]:
                    return field_type_map[base_type][node.attr]
                if base_type.startswith('vec'):
                    if all(c in 'xyzwrgba' for c in node.attr) and len(node.attr) <= 4:
                        if len(node.attr) == 1:
                            return 'float'
                        else:
                            return base_type
            return None
        if isinstance(node, ast.IfExp):
            body_type = self._infer_expr_type(node.body)
            orelse_type = self._infer_expr_type(node.orelse)
            if body_type == orelse_type:
                return body_type
            return None
        return None

    def _visit_subscript(self, node: ast.Subscript) -> str:
        if isinstance(node.value, ast.Name) and node.value.id == 'p':
            slice_expr = self._get_slice_expr(node.slice)
            idx = self._expr_to_glsl(slice_expr)
            return f"objectsIn[{idx}]"
        if isinstance(node.value, ast.Name) and node.value.id in self.var_types:
            var = node.value.id
            typ = self.var_types[var]
            slice_expr = self._get_slice_expr(node.slice)
            idx = self._expr_to_glsl(slice_expr)
            if typ.startswith('vec'):
                return f"{var}[{idx}]"
            elif typ.startswith('mat'):
                return f"{var}[{idx}]"
        if isinstance(node.value, ast.Subscript):
            base = self._visit_subscript(node.value)
            slice_expr = self._get_slice_expr(node.slice)
            idx = self._expr_to_glsl(slice_expr)
            return f"{base}[{idx}]"
        value = self._expr_to_glsl(node.value)
        slice_expr = self._get_slice_expr(node.slice)
        idx = self._expr_to_glsl(slice_expr)
        return f"{value}[{idx}]"

    def _get_slice_expr(self, slice_node: ast.AST) -> ast.AST:
        if isinstance(slice_node, ast.Index):
            return slice_node.value
        return slice_node

    # ---- Attribute access ----
    def _visit_attribute(self, node: ast.Attribute) -> str:
        if isinstance(node.value, ast.Name):
            var_name = node.value.id
            if var_name in self.var_types:
                typ = self.var_types[var_name]
                if typ and typ.startswith('vec'):
                    attr = node.attr
                    valid_sets = ['xyzw', 'rgba', 'stpq']
                    for s in valid_sets:
                        if all(c in s for c in attr) and len(attr) <= 4:
                            return f"{var_name}.{attr}"
        if isinstance(node.value, ast.Name) and node.value.id == 'p':
            mapping = {
                'x': 'position.x', 'y': 'position.y',
                'vx': 'velocity.x', 'vy': 'velocity.y',
                'mass': 'mass', 'charge': 'charge',
                'theta': 'visualData.z', 'omega': 'visualData.w',
                'r': 'color.r', 'g': 'color.g', 'b': 'color.b', 'a': 'color.a'
            }
            if node.attr in mapping:
                return mapping[node.attr]
            raise SyntaxError(f"Unknown p attribute: {node.attr}")
        if isinstance(node.value, ast.Subscript):
            if isinstance(node.value.value, ast.Name) and node.value.value.id == 'p':
                slice_expr = self._get_slice_expr(node.value.slice)
                idx_expr = self._expr_to_glsl(slice_expr)
                obj_ref = f"objectsIn[{idx_expr}]"
                field_map = {
                    'x': 'position.x', 'y': 'position.y',
                    'vx': 'velocity.x', 'vy': 'velocity.y',
                    'mass': 'mass', 'charge': 'charge',
                    'theta': 'visualData.z', 'omega': 'visualData.w',
                    'r': 'color.r', 'g': 'color.g', 'b': 'color.b', 'a': 'color.a'
                }
                if node.attr in field_map:
                    return f"{obj_ref}.{field_map[node.attr]}"
                raise SyntaxError(f"Unknown property for p[index]: {node.attr}")
        base_str = self._expr_to_glsl(node.value)
        if self._is_complex_expr(node.value):
            if node.attr == 'real':
                return f"real({base_str})"
            if node.attr == 'imag':
                return f"imag({base_str})"
            if node.attr == 'conjugate':
                return f"conj({base_str})"
        base_var = node.value
        base_str = self._expr_to_glsl(base_var)
        base_type = self._infer_expr_type(base_var)
        if base_type:
            field_map = {
                'Ray': {'origin': 'origin', 'direction': 'direction'},
                'Hit': {'hit': 'hit', 't': 't', 'point': 'point', 'normal': 'normal', 'uv': 'uv', 'material': 'material'},
                'GeoObject': {'type': 'type', 'data': 'data'},
                'CameraBasis': {'right': 'right', 'up': 'up', 'forward': 'forward'},
                'vec2': {'x': 'x', 'y': 'y'},
                'vec3': {'x': 'x', 'y': 'y', 'z': 'z'},
                'vec4': {'x': 'x', 'y': 'y', 'z': 'z', 'w': 'w'},
            }
            if base_type in field_map:
                field = node.attr
                if field in field_map[base_type]:
                    return f"{base_str}.{field_map[base_type][field]}"
                elif base_type.startswith('vec') and all(c in 'xyzwrgba' for c in field) and len(field) <= 4:
                    return f"{base_str}.{field}"
                raise SyntaxError(f"Unknown field {field} for type {base_type}")
        if isinstance(node.value, ast.Name):
            obj = node.value.id
            field_map = {
                'x': 'position.x', 'y': 'position.y',
                'vx': 'velocity.x', 'vy': 'velocity.y',
                'mass': 'mass', 'charge': 'charge',
                'theta': 'visualData.z', 'omega': 'visualData.w',
                'r': 'color.r', 'g': 'color.g', 'b': 'color.b', 'a': 'color.a'
            }
            if node.attr in field_map:
                if obj == 'color':
                    return field_map[node.attr]
                else:
                    return f"{obj}.{field_map[node.attr]}"
        raise SyntaxError(f"Unsupported attribute access: {node.attr}")

    # ------------------------------------------------------------------------
    # Expression translator
    # ------------------------------------------------------------------------
    def _expr_to_glsl(self, node: ast.AST) -> str:
        return self._expr_to_glsl_with_subst(node, {})

    def _expr_to_glsl_with_subst(self, node: ast.AST, subst: Dict[str, str]) -> str:
        if isinstance(node, ast.Constant):
            if isinstance(node.value, bool):
                return "true" if node.value else "false"
            if isinstance(node.value, complex):
                return f"vec2({node.value.real}, {node.value.imag})"
            return str(node.value)
        elif isinstance(node, ast.Name):
            name = node.id
            if name in subst:
                return subst[name]
            if name in self.imported_names:
                return name
            if name == 'i':
                self.complex_vars.add(name)
                self.var_types[name] = "vec2"
                return 'i'
            if name == 'num_objects':
                return 'uNumObjects'
            if name == 'dt':
                return 'uDt'
            if name == 'time':
                return 'uTime'
            if name == 'idx':
                return 'objectIndex' if self.mode != 'agent' else 'int(gl_GlobalInvocationID.x)'
            if name == 'group_count':
                return 'uGroupCount'
            if self.mode == 'agent' and name in ('signal_object_idx', 'signal_payload'):
                return name
            if name in ('x', 'y', 'vx', 'vy', 'mass', 'charge', 'theta', 'omega',
                        'color', 'pos', 'vel'):
                return name
            if self.mode == 'paint':
                if name == 'px':
                    return 'px'
                if name == 'py':
                    return 'py'
                if name == 'prev_r':
                    return 'prev_r'
                if name == 'prev_g':
                    return 'prev_g'
                if name == 'prev_b':
                    return 'prev_b'
                if name == 'prev_a':
                    return 'prev_a'
                if name == 't':
                    return 'uTime'
            if name in self.globals and isinstance(self.globals[name], (int, float)):
                return str(self.globals[name])
            return name
        elif isinstance(node, ast.UnaryOp):
            operand = self._expr_to_glsl_with_subst(node.operand, subst)
            if isinstance(node.op, ast.USub):
                return f"(-{operand})"
            elif isinstance(node.op, ast.Not):
                return f"(!{operand})"
            elif isinstance(node.op, ast.UAdd):
                return f"(+{operand})"
            elif isinstance(node.op, ast.Abs):
                if self._is_complex_expr(node.operand):
                    return f"length({operand})"
                return f"abs({operand})"
            else:
                raise SyntaxError("Unsupported unary operator")
        elif isinstance(node, ast.BinOp):
            left = self._expr_to_glsl_with_subst(node.left, subst)
            right = self._expr_to_glsl_with_subst(node.right, subst)
            left_is_c = self._is_complex_expr(node.left)
            right_is_c = self._is_complex_expr(node.right)

            if isinstance(node.op, ast.Pow):
                if left_is_c or right_is_c:
                    left_arg = left if left_is_c else f"vec2({left}, 0.0)"
                    right_arg = right if right_is_c else f"vec2({right}, 0.0)"
                    return f"cPow({left_arg}, {right_arg})"
                return f"safePow(float({left}), float({right}))"

            if isinstance(node.op, ast.Mult) and left_is_c and right_is_c:
                return f"cMul({left}, {right})"
            if isinstance(node.op, ast.Div) and left_is_c and right_is_c:
                return f"cDiv({left}, {right})"

            if isinstance(node.op, ast.MatMult):
                return f"({left} * {right})"

            op = self._bin_op(node.op)
            if isinstance(node.op, ast.Div):
                return f"(float({left}) / float({right}))"
            return f"({left} {op} {right})"
        elif isinstance(node, ast.Compare):
            if len(node.ops) != 1:
                raise SyntaxError("Chained comparisons not supported")
            left = self._expr_to_glsl_with_subst(node.left, subst)
            right = self._expr_to_glsl_with_subst(node.comparators[0], subst)
            op = self._compare_op(node.ops[0])
            return f"({left} {op} {right})"
        elif isinstance(node, ast.BoolOp):
            if isinstance(node.op, ast.And):
                op = "&&"
            elif isinstance(node.op, ast.Or):
                op = "||"
            else:
                raise SyntaxError("Unknown boolean operator")
            values = [self._expr_to_glsl_with_subst(v, subst) for v in node.values]
            return f"({f' {op} '.join(values)})"
        elif isinstance(node, ast.Call):
            func_name = node.func.id if isinstance(node.func, ast.Name) else None
            args = [self._expr_to_glsl_with_subst(a, subst) for a in node.args]

            # ---- Mode-specific enforcement ----
            if func_name == 'scratchpad_write':
                if self.mode != 'agent':
                    raise SyntaxError("scratchpad_write() is only available in 'agent' scripts.")
                if len(args) == 3:
                    return f"scratchpad_write(int({args[0]}), int({args[1]}), {args[2]})"
                raise SyntaxError("scratchpad_write(id, index, value)")

            if func_name == 'signal':
                if self.mode == 'agent':
                    raise SyntaxError("signal() cannot be used in 'agent' scripts (agents receive signals, they do not enqueue).")
                if len(args) == 2:
                    return f"signal_enqueue(int({args[0]}), {args[1]})"
                raise SyntaxError("signal(agent_id, payload)")

            if func_name in ('apply_constraints', 'detect_collision', 'resolve_collision'):
                if self.mode != 'object':
                    raise SyntaxError(f"{func_name}() is only available in 'object' scripts.")
                # these are handled elsewhere, but we still need to return something
                # The calls are handled in visit_Expr, so they will not appear here.

            # ---- Ray‑tracing ----
            if func_name == 'sphere':
                if len(args) == 2:
                    return f"sphere({args[0]}, {args[1]})"
                raise SyntaxError("sphere(center, radius)")
            if func_name == 'plane':
                if len(args) == 2:
                    return f"plane({args[0]}, {args[1]})"
                raise SyntaxError("plane(normal, d)")
            if func_name == 'intersect':
                if len(args) == 2:
                    return f"intersect({args[0]}, {args[1]})"
                raise SyntaxError("intersect(ray, object)")
            if func_name == 'miss_hit':
                return "miss_hit()"
            if func_name == 'closest_hit':
                if len(args) == 2:
                    return f"closest_hit({args[0]}, {args[1]})"
                raise SyntaxError("closest_hit(a, b)")
            if func_name == 'look_at':
                if len(args) == 3:
                    return f"look_at({args[0]}, {args[1]}, {args[2]})"
                raise SyntaxError("look_at(eye, target, up)")
            if func_name == 'camera_ray':
                if len(args) == 4:
                    return f"camera_ray({', '.join(args)})"
                else:
                    raise SyntaxError("camera_ray(eye, target, up, fov)")
            if func_name == 'camera_ray_ndc':
                if len(args) == 7:
                    return f"camera_ray_ndc({', '.join(args)})"
                else:
                    raise SyntaxError("camera_ray_ndc(ndcX, ndcY, eye, target, up, fov, aspect)")
            if func_name == 'offset_ray_origin':
                if len(args) == 2:
                    return f"offset_ray_origin({args[0]}, {args[1]})"
                raise SyntaxError("offset_ray_origin(position, normal)")
            if func_name == 'reflect':
                if len(args) == 2:
                    return f"reflect({args[0]}, {args[1]})"
                raise SyntaxError("reflect(direction, normal)")
            if func_name == 'refract':
                if len(args) == 3:
                    return f"refract({args[0]}, {args[1]}, {args[2]})"
                raise SyntaxError("refract(direction, normal, eta)")
            if func_name == 'raymarch':
                if len(args) >= 3:
                    if isinstance(node.args[1], ast.Name):
                        sdf_name = node.args[1].id
                        unique_name = f"raymarch_{sdf_name}"
                        if len(args) == 6:
                            return f"{unique_name}({args[0]}, {args[2]}, {args[3]}, {args[4]}, {args[5]})"
                        elif len(args) == 5:
                            return f"{unique_name}({args[0]}, {args[2]}, {args[3]}, {args[4]}, 1e-4)"
                        else:
                            raise SyntaxError("raymarch(ray, sdf, t_min, t_max, steps, eps)")
                    else:
                        raise SyntaxError("raymarch second argument must be a function name")
                else:
                    raise SyntaxError("raymarch(ray, sdf, t_min, t_max, steps, eps)")

            # ---- Scratchpad and signal (already handled above) ----
            if func_name == 'scratchpad_read':
                if len(args) == 2:
                    return f"scratchpad_read(int({args[0]}), int({args[1]}))"
                raise SyntaxError("scratchpad_read(id, index)")

            # ---- Derivative ----
            if func_name == 'diff':
                return self._emit_diff(node, subst)

            # ---- rand ----
            if func_name == 'rand' and len(args) == 0:
                offset = self.rand_call_counter
                self.rand_call_counter += 1
                return f"rand(vec2(uTime, float(objectIndex) + {offset}.0))"

            # ---- len ----
            if func_name == 'len':
                if len(args) != 1:
                    raise SyntaxError("len() takes exactly one argument")
                arg = node.args[0]
                if isinstance(arg, ast.Name) and arg.id in self.var_types:
                    typ = self.var_types[arg.id]
                    if typ.startswith('vec'):
                        return f"{arg.id}.length()"
                    elif typ.startswith('mat'):
                        size = int(typ[3])
                        return str(size * size)
                if isinstance(arg, ast.List):
                    return str(len(arg.elts))
                return "float(-1.0)"

            # ---- sum ----
            if func_name == 'sum':
                if len(args) != 1:
                    raise SyntaxError("sum() takes exactly one argument")
                arg = node.args[0]
                arg_str = self._expr_to_glsl(arg)
                typ = self._infer_expr_type(arg)
                if typ and typ.startswith('vec'):
                    n = int(typ[3])
                    comps = [f"{arg_str}.{chr(120+i)}" for i in range(n)]
                    return f"({' + '.join(comps)})"
                raise SyntaxError("sum() only works on vectors (vec2/vec3/vec4)")

            # ---- min/max with vector reduction ----
            if func_name == 'min':
                if len(args) == 1:
                    arg = node.args[0]
                    arg_str = self._expr_to_glsl(arg)
                    typ = self._infer_expr_type(arg)
                    if typ and typ.startswith('vec'):
                        n = int(typ[3])
                        comps = [f"{arg_str}.{chr(120+i)}" for i in range(n)]
                        res = comps[0]
                        for c in comps[1:]:
                            res = f"min({res}, {c})"
                        return res
                    raise SyntaxError("min() with one argument only works on vectors")
                elif len(args) == 2:
                    return f"min({args[0]}, {args[1]})"
                else:
                    raise SyntaxError("min() takes 1 or 2 arguments")

            if func_name == 'max':
                if len(args) == 1:
                    arg = node.args[0]
                    arg_str = self._expr_to_glsl(arg)
                    typ = self._infer_expr_type(arg)
                    if typ and typ.startswith('vec'):
                        n = int(typ[3])
                        comps = [f"{arg_str}.{chr(120+i)}" for i in range(n)]
                        res = comps[0]
                        for c in comps[1:]:
                            res = f"max({res}, {c})"
                        return res
                    raise SyntaxError("max() with one argument only works on vectors")
                elif len(args) == 2:
                    return f"max({args[0]}, {args[1]})"
                else:
                    raise SyntaxError("max() takes 1 or 2 arguments")

            # ---- pow, int, float ----
            if func_name == 'pow':
                if len(args) != 2:
                    raise SyntaxError("pow() takes exactly two arguments")
                return f"safePow({args[0]}, {args[1]})"

            if func_name == 'int':
                if len(args) != 1:
                    raise SyntaxError("int() takes exactly one argument")
                return f"int({args[0]})"

            if func_name == 'float':
                if len(args) != 1:
                    raise SyntaxError("float() takes exactly one argument")
                return f"float({args[0]})"

            # ---- Paint helper functions ----
            if self.mode == 'paint':
                if func_name == 'sample_prev_r':
                    return f"sample_prev_r(vec2(px, py), {args[0]})"
                if func_name == 'sample_prev_g':
                    return f"sample_prev_g(vec2(px, py), {args[0]})"
                if func_name == 'sample_prev_b':
                    return f"sample_prev_b(vec2(px, py), {args[0]})"
                if func_name == 'sample_prev_a':
                    return f"sample_prev_a(vec2(px, py), {args[0]})"
                if func_name == 'avg_prev_r':
                    return "avg_prev_r()"
                if func_name == 'avg_prev_g':
                    return "avg_prev_g()"
                if func_name == 'avg_prev_b':
                    return "avg_prev_b()"
                if func_name == 'avg_prev_a':
                    return "avg_prev_a()"

            # ---- Complex functions ----
            complex_funcs = {
                'cAdd': 'cAdd', 'cSub': 'cSub', 'cMul': 'cMul', 'cDiv': 'cDiv',
                'cLog': 'cLog', 'cExp': 'cExp', 'cPow': 'cPow',
                'cSin': 'cSin', 'cCos': 'cCos', 'cTan': 'cTan',
                'real': 'real', 'imag': 'imag', 'conj': 'conj', 'arg': 'arg'
            }
            if func_name in complex_funcs:
                return f"{complex_funcs[func_name]}({', '.join(args)})"

            # ---- Inline Python functions ----
            if func_name and func_name in self.globals:
                inlined = self._inline_function_call(func_name, args, node)
                if inlined is not None:
                    return inlined

            # ---- GLSL built‑in mapping ----
            glsl_map = self._glsl_func_map
            if func_name in glsl_map:
                return f"{glsl_map[func_name]}({', '.join(args)})"

            return f"{func_name}({', '.join(args)})"
        elif isinstance(node, ast.Subscript):
            return self._visit_subscript(node)
        elif isinstance(node, ast.Attribute):
            return self._visit_attribute(node)
        elif isinstance(node, ast.List):
            return self.visit_List(node)
        elif isinstance(node, ast.Tuple):
            elems = [self._expr_to_glsl_with_subst(e, subst) for e in node.elts]
            n = len(elems)
            if n in (2, 3, 4):
                return f"vec{n}({', '.join(elems)})"
            else:
                raise SyntaxError("Only tuples of length 2, 3, or 4 are supported as vector constructors")
        elif isinstance(node, ast.IfExp):
            return self.visit_IfExp(node)
        else:
            raise NotImplementedError(f"Unsupported expression: {type(node)}")

    _glsl_func_map = {
        'sin': 'sin', 'cos': 'cos', 'tan': 'tan',
        'asin': 'asin', 'acos': 'acos', 'atan': 'atan',
        'sinh': 'sinh', 'cosh': 'cosh', 'tanh': 'tanh',
        'asinh': 'asinh', 'acosh': 'acosh', 'atanh': 'atanh',
        'sqrt': 'sqrt', 'inversesqrt': 'inversesqrt',
        'log': 'log', 'log2': 'log2', 'exp': 'exp', 'exp2': 'exp2',
        'abs': 'abs', 'floor': 'floor', 'ceil': 'ceil',
        'frac': 'frac', 'fract': 'fract', 'sign': 'sign', 'step': 'step',
        'min': 'min', 'max': 'max', 'clamp': 'clamp',
        'mod': 'mod', 'atan2': 'atan2',
        'dot': 'dot', 'cross': 'cross', 'length': 'length', 'distance': 'distance',
        'normalize': 'normalize', 'reflect': 'reflect', 'refract': 'refract', 'faceforward': 'faceforward',
        'mix': 'mix', 'smoothstep': 'smoothstep',
        'radians': 'radians', 'degrees': 'degrees',
        'noise': 'noise', 'rand': 'rand', 'random': 'rand',
        'conjugate': 'conj',
        'transpose': 'transpose', 'inverse': 'inverse', 'determinant': 'determinant',
        'outerProduct': 'outerProduct', 'matrixCompMult': 'matrixCompMult',
        'saturate': 'saturate', 'lerp': 'lerp', 'fbm': 'fbm',
    }

    # ------------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------------
    def _emit_diff(self, node: ast.Call, subst: Dict[str, str]) -> str:
        if len(node.args) < 2:
            raise SyntaxError("diff() needs expr and wrt variable")
        expr_node = node.args[0]
        wrt_node = node.args[1]
        if not isinstance(wrt_node, ast.Name):
            raise SyntaxError("wrt must be a variable name")
        wrt = wrt_node.id
        order = 1
        if len(node.args) >= 3:
            order_arg = node.args[2]
            if isinstance(order_arg, ast.Constant) and isinstance(order_arg.value, int):
                order = order_arg.value
            else:
                raise SyntaxError("Order must be an integer constant")
        if order not in (1,2):
            raise SyntaxError("Only order 1 and 2 supported")
        plus_subst = dict(subst)
        plus_subst[wrt] = f"({wrt} + EPSILON)"
        minus_subst = dict(subst)
        minus_subst[wrt] = f"({wrt} - EPSILON)"
        expr_plus = self._expr_to_glsl_with_subst(expr_node, plus_subst)
        expr_minus = self._expr_to_glsl_with_subst(expr_node, minus_subst)
        if order == 1:
            return f"(({expr_plus} - {expr_minus}) / (2.0 * EPSILON))"
        else:
            expr_center = self._expr_to_glsl_with_subst(expr_node, subst)
            return f"(({expr_plus} - 2.0*{expr_center} + {expr_minus}) / (EPSILON*EPSILON))"

    def _inline_function_call(self, func_name: str, args: List[str], node: ast.Call) -> Optional[str]:
        if func_name not in self.globals:
            return None
        func_obj = self.globals[func_name]
        if not callable(func_obj):
            return None
        if func_name in self.inlined_functions:
            return None
        if self.inline_depth >= self.max_inline_depth:
            return None
        try:
            source = inspect.getsource(func_obj)
        except (TypeError, OSError):
            return None
        tree = ast.parse(textwrap.dedent(source))
        if not isinstance(tree.body[0], ast.FunctionDef):
            return None
        func_def = tree.body[0]
        if len(func_def.body) != 1 or not isinstance(func_def.body[0], ast.Return):
            return None
        param_names = [arg.arg for arg in func_def.args.args]
        if len(args) != len(param_names):
            return None
        subst = dict(zip(param_names, args))
        self.inlined_functions.add(func_name)
        self.inline_depth += 1
        result = self._expr_to_glsl_with_subst(func_def.body[0].value, subst)
        self.inline_depth -= 1
        self.inlined_functions.remove(func_name)
        return result

    def _bin_op(self, op: ast.AST) -> str:
        if isinstance(op, ast.Add): return "+"
        if isinstance(op, ast.Sub): return "-"
        if isinstance(op, ast.Mult): return "*"
        if isinstance(op, ast.Div): return "/"
        if isinstance(op, ast.FloorDiv): return "/"
        if isinstance(op, ast.MatMult): return "*"
        raise SyntaxError("Unsupported binary operator")

    def _aug_op(self, op: ast.AST) -> str:
        if isinstance(op, ast.Add): return "+"
        if isinstance(op, ast.Sub): return "-"
        if isinstance(op, ast.Mult): return "*"
        if isinstance(op, ast.Div): return "/"
        raise SyntaxError("Unsupported augmented assignment")

    def _compare_op(self, op: ast.AST) -> str:
        if isinstance(op, ast.Lt): return "<"
        if isinstance(op, ast.LtE): return "<="
        if isinstance(op, ast.Gt): return ">"
        if isinstance(op, ast.GtE): return ">="
        if isinstance(op, ast.Eq): return "=="
        if isinstance(op, ast.NotEq): return "!="
        raise SyntaxError("Unsupported comparison operator")


# -----------------------------------------------------------------------------
# AssignCollector – collects target variables assigned within Python AST
# -----------------------------------------------------------------------------
class AssignCollector(ast.NodeVisitor):
    def __init__(self) -> None:
        self.vars: Set[str] = set()
        self._is_root = True

    def _collect_target(self, target: ast.AST) -> None:
        if isinstance(target, ast.Name):
            self.vars.add(target.id)
        elif isinstance(target, (ast.Tuple, ast.List)):
            for elt in target.elts:
                self._collect_target(elt)

    def visit_Assign(self, node: ast.Assign) -> None:
        for target in node.targets:
            self._collect_target(target)
        self.generic_visit(node)

    def visit_AugAssign(self, node: ast.AugAssign) -> None:
        self._collect_target(node.target)
        self.generic_visit(node)

    def visit_For(self, node: ast.For) -> None:
        self._collect_target(node.target)
        self.generic_visit(node)


# -----------------------------------------------------------------------------
# User‑facing decorator
# -----------------------------------------------------------------------------
def script(sim: Any, debug: bool = False, mode: str = 'object') -> Callable:
    """
    Decorator that compiles a Python function into a GPU compute shader.

    Args:
        sim: The Simulation instance (used to register the compiled shader).
        debug: If True, prints the generated shader source and enables warnings.
        mode: One of 'object' (default), 'paint', or 'agent'.

    Usage:
        @sim.script(mode='paint')
        def my_paint():
            color.r = 0.5
            color.g = 0.3
            color.b = 0.8
            color.a = 1.0
    """
    def decorator(func: Callable) -> Callable:
        generator = GLSLGenerator(debug=debug, mode=mode)
        shader_source = generator.generate(func)

        if mode == 'agent':
            script_id = sim.register_agent(shader_source)   # marks as agent
        else:
            script_id = sim.register_script(shader_source)
        func._script_id = script_id
        func._mode = mode
        return func
    return decorator