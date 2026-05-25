# 2026-05-25 — SecureOS Scripting Language (sosh)

## Goal

Add a minimal scripting language ("sosh" — SecureOS Shell) that can:

1. Run executables and capture their output into variables
2. Define and concatenate string variables
3. Evaluate `if`/`else` conditionals based on variable values
4. Loop (`while`, `for`) over values or until a condition is met
5. Accept command-line arguments as positional parameters

This enables startup scripts, driver-loading scripts, and user automation
without hard-coding any policy into the kernel.

## Why not bash?

Bash is far too large and complex to embed (even a POSIX sh requires fork,
exec, pipes, signal handling, and a full libc). Instead we design a
purpose-built interpreter that:

- Runs in a freestanding (no libc) kernel/userspace environment
- Has zero dynamic memory allocation (fixed buffers, bounded scope)
- Integrates with the existing capability system for exec gating
- Is small enough to audit line-by-line (~1500–2500 LOC target)

## Syntax Overview

```sosh
#!/sosh
# Comments start with #

# --- Variables ---
set NAME = "SecureOS"
set VERSION = "0.1"
set GREETING = "Welcome to " + $NAME + " v" + $VERSION

# --- Command output capture ---
set FILES = $(ls /apps)
set TODAY = $(date)

# --- Positional args ---
# $0 = script name, $1..$9 = arguments, $@ = all args
set TARGET = $1

# --- Conditionals ---
if $TARGET == "debug"
  echo "Debug mode enabled"
  set LOGLEVEL = "verbose"
elif $TARGET == "release"
  echo "Release build"
  set LOGLEVEL = "error"
else
  echo "Unknown target: " + $TARGET
  set LOGLEVEL = "info"
end

# --- Loops ---
# for-in loop (iterate over space-separated tokens)
for FILE in $(ls /drivers)
  echo "Loading driver: " + $FILE
  loaddriver $FILE
end

# while loop
set COUNT = 0
while $COUNT < 5
  echo "Tick " + $COUNT
  set COUNT = $COUNT + 1
end

# --- Exec a command ---
echo $GREETING
myapp --flag $TARGET
```

## Language Specification

### Data Model

- **All values are strings.** Numeric operations (comparison, arithmetic)
  parse strings as integers when needed and produce string results.
- **No arrays.** For-in iterates over space-separated tokens in a string.
- **Variables are scoped to the script invocation.** No global shared state
  between script runs (env vars are separate, accessed via `$ENV.NAME`).

### Statements (one per line, no semicolons)

| Statement | Syntax | Semantics |
|-----------|--------|-----------|
| Comment | `# ...` | Ignored |
| Set | `set VAR = expr` | Assign variable |
| If | `if cond` / `elif cond` / `else` / `end` | Conditional |
| While | `while cond` / `end` | Loop while true |
| For | `for VAR in expr` / `end` | Iterate tokens |
| Exec | `command args...` | Run external binary |
| Return | `return [code]` | Exit script with code |

### Expressions

- String literal: `"hello world"` or bare word (no spaces): `hello`
- Variable ref: `$VAR`, `$1`, `$@`, `$?` (last exit code)
- Command capture: `$(command args...)`
- Concatenation: `expr + expr`
- Arithmetic: `expr + expr`, `expr - expr` (when both sides parse as int)

### Conditions

- `expr == expr` — string equality
- `expr != expr` — string inequality
- `expr < expr`, `expr > expr`, `expr <= expr`, `expr >= expr` — numeric comparison
- `exists path` — file/dir existence check
- `not cond` — negation

### Built-in Commands (available without exec)

These mirror what's already in the `.cmd` interpreter but are now first-class:

- `echo expr...` — print to stdout
- `set` — variable assignment (as above)
- `export VAR = expr` — set an environment variable visible to child processes
- `source path` — execute another script in the current scope
- `exit [code]` — terminate script

## Architecture

### Component Placement

```
user/libs/soshlib/          # The interpreter library (standalone .a / ELF)
├── sosh_lexer.c            # Tokenizer: line → tokens
├── sosh_lexer.h
├── sosh_parser.c           # Parser: tokens → AST nodes (flat array)
├── sosh_parser.h
├── sosh_eval.c             # Evaluator: walk AST, execute statements
├── sosh_eval.h
├── sosh_vars.c             # Variable storage (fixed hash table)
├── sosh_vars.h
├── sosh_builtins.c         # Built-in command handlers (echo, set, etc.)
├── sosh_builtins.h
└── sosh.h                  # Public API header

user/os_commands/sosh.cmd   # Thin wrapper: "sosh $ARGS" (legacy compat)

user/apps/sosh/             # The sosh binary (uses soshlib)
└── main.c                  # Entry: parse script file, invoke interpreter
```

### Why a library?

Per project conventions, functionality useful to multiple applications must be
a standalone library. The interpreter logic lives in `soshlib` so that:

- The kernel can embed a minimal "boot script runner" if needed
- Other apps can evaluate script snippets (e.g., config files)
- The `sosh` binary is just a thin CLI wrapper

### Execution Flow

```
┌──────────────────────────────────────────────────────────┐
│  console.c  (user types: `sosh /scripts/startup.sh`)     │
│      │                                                    │
│      ▼                                                    │
│  launcher_exec.c  → loads /apps/sosh ELF                 │
│      │                                                    │
│      ▼                                                    │
│  sosh/main.c  → reads script file, calls sosh_eval()     │
│      │                                                    │
│      ▼                                                    │
│  soshlib/sosh_eval.c  → interprets line by line           │
│      │                                                    │
│      ├─ built-in (echo/set/if/while) → handled internally│
│      │                                                    │
│      └─ external command → calls back to process_run()   │
│         via a function pointer (exec callback)            │
└──────────────────────────────────────────────────────────┘
```

### Memory Model (Zero Dynamic Allocation)

| Resource | Limit | Rationale |
|----------|-------|-----------|
| Variables | 64 slots | More than enough for boot scripts |
| Variable name | 32 chars | |
| Variable value | 256 chars | Concatenation truncates at limit |
| Nesting depth (if/while/for) | 16 levels | Prevents stack overflow |
| Script line length | 512 chars | Matches existing APP_LINE_MAX |
| Call stack (source) | 4 levels | Prevents infinite recursion |
| Captured output | 512 chars | Matches CONSOLE_OUTPUT_MAX |

### Capability Integration

The sosh interpreter inherits the capabilities of the invoking process. When
a script calls an external command, the exec callback passes through the same
`process_context_t` — the capability gate in `launcher_exec.c` still checks
`CAP_APP_EXEC` for each binary. No new capability types are needed for phase 1.

Future: A `CAP_SCRIPT_EXEC` capability could restrict which processes are
allowed to run scripts at all (defense in depth).

## Phased Implementation

### Phase 1 — Core interpreter (this plan)

- [ ] `soshlib`: lexer, parser, evaluator with variables + string concat
- [ ] `soshlib`: `if`/`elif`/`else`/`end` conditionals
- [ ] `soshlib`: `while`/`end` and `for VAR in`/`end` loops
- [ ] `soshlib`: `$()` command output capture via exec callback
- [ ] `soshlib`: positional parameters `$0`–`$9`, `$@`, `$?`
- [ ] `user/apps/sosh/main.c`: script file runner binary
- [ ] Build integration (build scripts, disk image inclusion)
- [ ] Unit tests for lexer, parser, evaluator

### Phase 2 — Boot/startup integration

- [ ] Define `/scripts/startup.sosh` convention
- [ ] Kernel calls sosh on startup script before entering interactive console
- [ ] Move driver loading from hard-coded kernel calls to startup script
- [ ] Move library preloading to startup script

### Phase 3 — Interactive mode

- [ ] `sosh` without arguments enters interactive REPL mode
- [ ] Line editing, history (reuse console infrastructure)
- [ ] Tab completion for commands and file paths

### Phase 4 — Advanced features (future)

- [ ] Functions (`fn name` / `end`)
- [ ] Pipes (`cmd1 | cmd2`) once IPC supports streams
- [ ] Background execution (`cmd &`) once scheduler supports it
- [ ] Signal/interrupt handling
- [ ] Here-documents for multi-line strings

## Example: Startup Script

```sosh
#!/sosh
# /scripts/startup.sosh — SecureOS boot initialization

echo "Initializing SecureOS..."

# Load core drivers
for DRV in serial keyboard vga storage
  echo "  Loading driver: " + $DRV
  loaddriver $DRV
end

# Load shared libraries
for LIB in fslib envlib timelib netlib
  loadlib $LIB
end

# Set default environment
export PATH = "/apps:/os"
export HOME = "/"
export SHELL = "/apps/sosh"

# Run user startup if it exists
if exists /user/startup.sosh
  source /user/startup.sosh
end

echo "System ready."
```

## Testing Strategy

- **Unit tests** (`tests/sosh_*.c`): exercise lexer/parser/eval in isolation
  with mock exec callbacks. No real filesystem needed.
- **Integration tests** (`tests/test_sosh_script.sh` + `.ps1`): run sosh
  binary in QEMU with known scripts, assert serial output.
- **Regression**: the existing `.cmd` format continues to work unchanged;
  sosh is additive.

## Compatibility

- **`.cmd` files**: unchanged. The existing simple interpreter remains for
  backwards compatibility. Eventually `.cmd` can delegate to sosh internally.
- **File extension**: `.sosh` for new-style scripts. The launcher detects
  the `#!/sosh` shebang or the `.sosh` extension.
- **No kernel changes for phase 1**: sosh is a userspace app + library. The
  kernel only gains startup-script support in phase 2.

## Open Questions

1. **Arithmetic precision**: 32-bit or 64-bit integers? Recommend 32-bit
   (matches the kernel's `uint32_t` world).
2. **Error handling**: should a failed command abort the script by default
   (like `set -e`) or continue? Recommend continue + check `$?`.
3. **String escaping**: support `\n`, `\t`, `\\` in string literals?
   Recommend yes for phase 1.
