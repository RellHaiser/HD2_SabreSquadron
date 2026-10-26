# Hidden & Dangerous 2 — Mission Scripting Language

Reference for the mission script language of *Hidden & Dangerous 2: Sabre Squadron*
(`*.scr` files), written from the C++ source port in [`src/script`](../src/script)
and checked against the original executable and the game's own scripts.

**Sources and how far each statement is verified**

| Source | Used for |
|---|---|
| Port: [`src/script`](../src/script) (compiler, expressions, interpreter, actor scripts, script module) | The primary description of every rule |
| Original `HD2_SabreSquadron_unpacked.exe` decompilation (`_re/decomp`, live Ghidra) | Every bug below marked *original* was confirmed in the original code (addresses given) |
| The game's 5,347 shipped scripts (`H:\HD2_Unpacked\SCRIPTS`) and 79 `scripts.dta` / `mpscripts.dta` files | Real syntax, usage counts, bugs that actually occur in shipped content |
| A test harness that runs the port's real compiler and expression code on its own (Appendix B) | Every *port* bug and every expression example was reproduced, not inferred |

Usage counts ("Uses") are the number of times a command appears in the **4,793 shipped
scripts that are referenced by a `scripts.dta` and compile**.

**Notation.** Argument types: **I** Integer, **S** String, **F** Frame. Bug references
such as [P1](#p1) or [R5](#r5) point to [section 10](#10-bugs-and-quirks).

---

## Contents

1. [Overview](#1-overview)
2. [Lexical rules](#2-lexical-rules)
3. [Values, variables and declarations](#3-values-variables-and-declarations)
4. [Statements](#4-statements)
5. [Expressions](#5-expressions)
6. [Execution model](#6-execution-model)
7. [Built-in functions](#7-built-in-functions)
8. [Commands](#8-commands)
9. [Compiled form](#9-compiled-form)
10. [Bugs and quirks](#10-bugs-and-quirks)
- [Appendix A — Opcodes outside the command table](#appendix-a--opcodes-outside-the-command-table)
- [Appendix B — How the findings were verified](#appendix-b--how-the-findings-were-verified)

---

## 1. Overview

### 1.1 What a mission script is

A mission script is a small text program attached to **one frame** of the mission's scene
(a soldier, a dog, a dummy, a door, a camera, a sound...). At mission start every script
is compiled into a list of commands and then run **cooperatively, one command per game
tick**, next to all the other scripts. Scripts react to events (death, alarm, signals from
other scripts, cutscenes, being used or hit) through *handlers* and to polled conditions
through *Whenever* blocks.

Key facts:

- **Server only.** Scripts compile and run only where the game is the server (single player
  is a local server). Commands with visible effects send network messages to the clients
  ([6.11](#611-network-play)).
- **Integers, strings and frames** are the only value types; expressions are integer only.
- **No functions or local scopes.** Code is organised with labels, `GoTo`, `Gosub`/`Return`,
  `If`/`Else` and `{ }` blocks; all variables are global to the script.
- **Errors are silent.** A script that fails to compile is simply not run; nothing is
  reported ([C9](#c9)).

### 1.2 Files and how scripts get attached

| File | Content |
|---|---|
| `Missions\<mission>\scripts.dta` | Script list for local (single player) games: a chunk file whose root chunk holds alternating id-1 chunks *frame name*, *script name* (NUL-terminated). With the next file, 5,991 assignments in 79 files ship with the game. |
| `Missions\<mission>\mpscripts.dta` | The same, used instead for network games, co-op included (`GAME_ROLE_LOCAL` not set). |
| `Scripts\<mission>\<name>.scr` | Script source. `.scr` is appended to the name when missing. |
| `Scripts\<mission>\<file>` then `Scripts\include\<file>` | `#include` lookup order ([4.10](#410-include)). |

At mission start ([`StartMission`](../src/script/script_module.cpp#L330)) every entry whose
frame exists in the scene is compiled and started (the list is walked backwards; entries
for missing frames are dropped). Scripts can attach, replace or remove scripts at run time
with `ScriptAssign` ([8.1](#81-script-control)). Some script folders also hold `.ver` files
(700 in all; the one compared is identical to its `.scr`), apparently editor or
version-control copies; the script loader only opens `.scr` files.

Source files are read through the data archive layer (`dtaOpen`) and **truncated to
65,534 bytes** ([C15](#c15)).

### 1.3 Script kinds

The frame's actor decides which class runs the script
([`AssignScript`](../src/script/script_module.cpp#L551)):

| Frame's actor | Class | Extra commands |
|---|---|---|
| Human (`ACTOR_HUMAN`) | `C_HumanScript` | `HUMAN_*`, `FRAME_SETAIMODE_*`, `HUMAN_ACTIVITY_*` |
| Dog (`ACTOR_MELEE_TRACKER`) | `C_MeleeTrackerScript` | `DOG_*` |
| Anything else, or no actor | `C_Script` ("generic") | none |

Using a `HUMAN_*` or `DOG_*` command in a generic script **stops that script for good**
([R7](#r7)). In a human script, `DOG_*` commands are skipped, and vice versa.

When a human whose controller is the script-driven AI (controller type 5) gets a script,
the script is handed to its scripted controller; player-team humans are marked
"scripted". Many human commands only act while the human is under that controller.

### 1.4 Anatomy of a typical script

The shipped scripts follow one pattern almost without exception. An illustrative example:

```
// guard_01 - guard at the gate
block{                                   // setup runs in a single tick
  FRAME  post;      FRM_FindFrame(post, "gate_post_01");
  INTEGER alarmType = 0;
  SetAlarmType(1023, true);              // react to every alarm kind (8.15)
}

OnDeath(){                               // event handlers: skipped by normal flow
  EndScript();
}

OnSignal(5){                             // another script sent signal 5
  HUMAN_Suspend(false);
  goto PATROL;
}

Whenever near (_PlayerInRange(30)){      // polled every 500 ms
  goto ALERT;
}

OnAlarm(){
  alarmType = _GetAlarmType();
  if(alarmType == 32){                   // 32 = sees an enemy
    HUMAN_SETMODE_Run();
  }
  goto ALERT;
}

  HUMAN_SETMODE_Guard();                 // main code starts after the handlers
  HUMAN_Suspend(true);
goto END;

Label PATROL:
  HUMAN_Move("guard01_a");               // waits until the soldier arrives
  HUMAN_Move("guard01_b");
  Delay(3000);
goto PATROL;

Label ALERT:
  HUMAN_TurnAtNearestPlayer();
  Delay(1000);
goto ALERT;

Label END:
```

Execution starts at the first command. Handler and `Whenever` declarations met on the way
are jumped over (one tick each), so the main code effectively starts below them. Reaching
the end of the file leaves the script idle but still listening for events.

---

## 2. Lexical rules

The lexer is [`NextToken`](../src/script/script_compiler.cpp#L225).

| Element | Rule |
|---|---|
| Case | Keywords, command names, function names, variables and labels are **case-insensitive** (`INTEGER`, `block{`, `goto`, `Human_Move` all appear in shipped scripts). |
| Whitespace | Space, tab, CR, LF. Only **CR** advances the line counter used for error positions, so LF-only files report line 0 ([C11](#c11)). |
| Names | A letter or `_`, then letters, digits, `_`. |
| Numbers | A run of digits and `.` (`1.5` is one token, `1.2.3` too). No signs, exponents or hex. Values are truncated to integers (`1.9` → 1). |
| Strings | `"..."` on one line; no escapes. An unterminated string ends at the line end. String literals (command arguments and expressions) pass through `printf` formatting with no arguments, so `%%` means `%` and a lone `%` is dropped (shipped scripts rely on this: [P1](#p1)). |
| Comments | `//` to the end of the line. **Only allowed between statements** ([C4](#c4)). There are no block comments. |
| Punctuation | Every other character is a token by itself: `( ) { } ; , : = + - * / < > !`. Inside expressions `==`, `<=`, `>=`, `!=` and `<>` are joined from two tokens (so `= =` also works). |
| Encoding | Bytes; text after a NUL byte is never read. |

---

## 3. Values, variables and declarations

### 3.1 Types

| Type | Declared with | Storage | Initial value | Notes |
|---|---|---|---|---|
| Integer | `Integer name [= expr];` | number table | 0 | 32-bit signed. Also used for booleans (0 = false). |
| String | `String name [= expr];` | string table | `""` | Not saved in saved games ([R13](#r13)). Not used by any shipped script. |
| Frame | `Frame name;` | frame table | null | A reference-counted scene frame. No initializer. |
| Bool, Enum | `Bool name;` | **none** | — | Accepted, but the name has no storage: reads give 0, writes are ignored ([C7](#c7)). |
| Real | — | — | — | `Real` is a keyword but **not** a declaration: `Real x;` is a compile error ([C7](#c7)). |

Two internal types exist for command arguments: *expression* (the condition of `If` and
`Whenever`) and *Whenever name*.

### 3.2 Declarations

- Declarations can appear anywhere, including inside blocks and handlers, but a name must
  be **declared before it is used** (the compiler is single-pass).
- All names live in **one namespace per script**: declaring a name twice is an error, and
  a later `Whenever` with the name of a variable hides that variable ([C13](#c13)).
- A declaration emits no code. Its `= expr` initializer becomes an ordinary assignment
  command **at the declaration's position**, executed only when control passes there.
  Shipped scripts therefore put their declarations in a `block{ ... }` at the top, so the
  whole setup runs in one tick.

### 3.3 Literals

| Literal | Where | Meaning |
|---|---|---|
| `123`, `1.5` | Integer arguments, expressions | Truncated integer. |
| `-5` | Integer arguments (handled specially), expressions (with caveats, [E5](#e5)) | Negative number. |
| `true`, `false` | **Integer command arguments only** | 1 / 0. Not accepted inside expressions ([E10](#e10)). |
| `"text"` | String arguments, string expressions | Text. Identical literals are shared, case-insensitively ([C2](#c2)). |

There are no frame literals: frames come from commands such as `FRM_FindFrame` and
`FRM_GetMyFrame`.

### 3.4 Arguments are passed by reference

A command argument is stored as an **index into the script's variable tables**, never as a
value. Each numeric literal gets its own hidden variable, so commands that write to an
Integer argument (`FRM_CreateIndexedParticle`) or a Frame argument (`FRM_FindFrame`,
`CSC_GetPlayerNum`) store their result in the variable that was passed.

---

## 4. Statements

### 4.1 Summary

| Statement | Syntax | Ends with | Notes |
|---|---|---|---|
| Declaration | `Integer n = 5;` `String s;` `Frame f;` | `;` | [3.2](#32-declarations) |
| Assignment | `name = expression;` | `;` | Integer or String target |
| Command | `NAME(arg, arg, ...);` | `;` | Exact argument count |
| Label | `Label name:` | `:` | Keyword is required |
| Jump | `GoTo name;` `Gosub name;` `Return;` | `;` | |
| Condition | `If (expr) { ... } [Else { ... }]` | block | No `Else If` |
| Block | `{ ... }` | `}` | Grouping only |
| Atomic block | `Block { ... }` | `}` | Body runs in one tick |
| Whenever | `Whenever name (expr) { ... }` | block | Polled condition |
| Whenever control | `SetWhenever(name, 0/1);` `SetWheneverTime(name, ms);` | `;` | Literal second argument |
| Handlers | `OnDeath() { ... }` `OnSignal(3) { ... }` ... | block | [4.8](#48-event-handlers) |
| Start point | `ScriptStartPoint:` | `:` | Unused by shipped scripts |
| Include | `#include "file.scr"` | line | Top of file only in the original; start of any line in the port |
| Comment | `// ...` | line end | Between statements only |

A statement may start only with a keyword, a command name or (for an assignment) a
variable name. Built-in functions (`_PlayerInRange` etc.) are only valid inside
expressions.

### 4.2 Commands and their arguments

```
FRM_SetOn(door_frame, false);
HUMAN_Move("checkpoint_12");
SendSignal(guard, 3);
```

- The number of arguments must match the command table exactly ([8](#8-commands)); extra
  or missing arguments are a compile error (38 shipped CASTLE1 scripts fail this way:
  [S3](#s3)). Commands without arguments still need `()`.
- Each argument must be a **variable of the parameter's type or a literal**: Integer
  parameters take an Integer variable, a number, `-number`, `true` or `false`; String
  parameters take a String variable or a `"literal"`; Frame parameters take a Frame
  variable. **Expressions are not allowed as arguments**: compute into a variable first.
- Commas between arguments are required.

### 4.3 Assignment

```
count = count + 1;
name  = "cp_" + count;        // String target: "+" concatenates (5.6)
```

The target must be a declared Integer or String. Assigning to a Frame, Bool, Enum or
Whenever name compiles but does nothing.

### 4.4 Labels and jumps

```
Label LOOP:
  ...
GoTo LOOP;

Gosub SAY_HELLO;              // pushes the address of the next command
...
Label SAY_HELLO:
  ...
Return;
```

- `Label name:` marks the next command. Labels may be used before they are defined.
  Undefined or doubly defined labels are compile errors. Labels have their own namespace.
- `Gosub` pushes a return address (at most **100** nested calls; the 101st `Gosub` stops
  the script, [R5](#r5)). `Return` pops it; **`Return` with an empty stack ends the
  script** like `EndScript` ([R6](#r6)).
- Inside `Block { }`, `GoTo`, `Gosub`, `Return` and `Delay` are **silently ignored**
  ([R2](#r2)).
- Every jump costs one tick; the target command runs on the next tick.

### 4.5 If / Else

```
If (x == 1) {
  ...
} Else {
  If (x == 2) {               // there is no "Else If"
    ...
  }
}
```

- The condition is any expression; non-zero is true.
- A `{ }` block must follow the condition (an `If` without one is a compile error).
- `Else` must directly follow the `}` of the `If` and be followed by a block.
  `Else If (...)` is a compile error; nest instead.
- A comment between `)` and `{` breaks the statement ([C4](#c4)); put it after `{`.

### 4.6 Blocks and `Block`

`{` and `}` are statements of their own: each compiles into a command that records the
size of its body. A plain `{ }` only groups; **each brace costs a tick** when executed.

`Block { ... }` (written `block{` in most shipped scripts) runs its whole body **within
one tick**, which is how scripts do atomic setup. Inside such a block:

| Construct | Behaviour inside `Block` |
|---|---|
| Assignments, ordinary commands, `If` / `Else` | Run normally |
| Nested `{ }` and `Block` | Flattened into the outer block |
| `GoTo`, `Gosub`, `Return`, `Delay` | **Ignored** ([R2](#r2)) |
| Commands that wait (moves, attacks, activities, `HUMAN_Suspend(true)`, delayed animation / speech) | Started but **not waited for**; some leave side effects after the block ([R4](#r4)) |
| `Whenever` declaration | Skipped, together with **the command after its body** ([R3](#r3)) |
| Handler declarations | Their bodies run inline (do not do this) |
| `EndScript` | Marks the script finished, but the rest of the block still runs |

### 4.7 Whenever

```
Whenever near (_PlayerInRange(30)) {
  ...
  SetWhenever(near, true);    // re-arm, otherwise it fires only once
}
```

A Whenever is a **named, polled condition**. Its name joins the variable namespace. The
script's normal flow skips the body. Every 500 ms of game time (default period) the
condition is evaluated; when it is true the Whenever **fires**:

- the instruction pointer jumps to the body's `{`, abandoning whatever the script was
  doing (a pending `Delay` is cancelled, a running move continues in the actor but the
  script no longer waits for it);
- the Whenever **disables itself**. `SetWhenever(name, true)` re-arms it,
  `SetWhenever(name, false)` disables it, and `SetWheneverTime(name, ms)` changes its
  period;
- at the end of the body execution simply continues with the next command in the file.
  Bodies usually end with `goto`.

Rules and limits:

- The second argument of `SetWhenever` / `SetWheneverTime` must be a **literal**
  ([C6](#c6)).
- `SetWhenever` may name a Whenever defined further down the file (476 shipped scripts do);
  `SetWheneverTime` may not ([C5](#c5)).
- The compiler does not check that a body follows the condition ([C3](#c3), one shipped
  case: [S5](#s5)).
- `DisableWhenevers(true)` stops all polling. The timing details are in
  [6.8](#68-whenever-scheduling).

### 4.8 Event handlers

| Handler | Fired by | Wakes a finished script? | Cancels `Delay`? | Notes |
|---|---|---|---|---|
| `OnDeath()` | The script's actor dies | yes | yes | Without an `OnDeath` handler the script ends when the actor dies. |
| `OnAlarm()` | The actor raises an alarm (see `SetAlarmType`, [8.15](#815-alarm-type-flags)) | no | yes | Suppressed by `DisableAlarms(true)`. Does not interrupt the first tick of `OnDeath`. |
| `OnAlarmDone()` | The alarm is over | yes | yes | The AI asks whether the script handles it. |
| `OnUse()` | A player uses the frame | **no** | **no** | Deferred until a running `Delay` ends ([R9](#r9)). |
| `OnHit()` | A shot hits the frame | **no** | **no** | Same as `OnUse`. Body not checked by the compiler. |
| `OnGlobalAlarm()` | Broadcast to every script | **no** | **no** | Not used by shipped scripts. Body not checked by the compiler. |
| `OnSignal(n)` | `SendSignal(frame, n)` from any script | **no** | yes | See [6.7](#67-signals). |
| `OnCutscene(n)` | `CSC_RunCutscene(n)` | yes | yes | Only scripts inside an `OnCutscene` handler run during the cutscene ([6.9](#69-cutscenes)). |
| `OnCutsceneDone(n)` | `CSC_EndCutscene()` | yes | yes | |
| `OnExit()` | The script is about to be destroyed (mission stop, restart, `ScriptAssign`) | — | — | Its body runs **immediately and atomically**, like a `Block`. |

General rules:

- `OnSignal`, `OnCutscene` and `OnCutsceneDone` take a non-negative number literal; the
  others take `()`. `OnDeath{` without `()` is a compile error (3 shipped scripts:
  [S2](#s2)).
- Duplicate handlers compile, but only one of them ever runs: the **first** for
  `OnSignal` / `OnCutscene` / `OnCutsceneDone` with the same number, the **last** for the
  others.
- Firing a handler **moves the instruction pointer** to its `{`. The previous position is
  lost (there is no "return from handler"), but the `Gosub` stack is kept, so a `Return`
  inside a handler returns to the last caller ([R6](#r6)).
- The "do not interrupt `OnDeath` / `OnAlarm`" protection only covers the single tick in
  which that handler starts ([R9](#r9)).

### 4.9 ScriptStartPoint

`ScriptStartPoint:` marks where the script starts and restarts (default: the first
command). Code above it, **including initializers of declarations**, never runs unless
jumped to. At most one per script. No shipped script uses it.

### 4.10 #include

```
#include "common_part.scr"
```

The directive is replaced by the contents of `Scripts\<mission>\<file>`, or failing that
`Scripts\include\<file>`. In the original game this works **only for a directive at the
very top of the file** ([C1](#c1)); all 7 shipped uses are on line 1 and include a file
from the mission's own folder (there is no `Scripts\include` folder in the shipped data).
The port expands a directive at the start of any line (after optional spaces or tabs);
one behind `//` or inside a string literal is left alone ([P3](#p3)).

---

## 5. Expressions

Expressions appear in assignments and in `If` / `Whenever` conditions. They are compiled
by [`C_ScriptExpression`](../src/script/script_expression.cpp) into a tree of integer
operations.

### 5.1 Operands

| Operand | Value |
|---|---|
| Integer variable | Its value. |
| Number literal | Its value (negative literals: [5.5](#55-negative-numbers)). |
| Built-in function call | Its result ([7](#7-built-in-functions)). |
| `( sub-expression )` | Its value. |
| String variable or literal | 0 in integer context; text in string context ([5.6](#56-string-expressions)). |
| Frame variable, Bool / Enum name | 0. |

An unknown name **ends the expression silently** instead of raising an error ([C8](#c8)).

### 5.2 Operators and precedence

| Class (tightest first) | Operators | Notes |
|---|---|---|
| 3 | `*` `/` | Integer division, truncating. `/ 0` crashed the original; the port gives 0 ([F1](#f1)). |
| 2 | `+` `-` | |
| 1 | `>` `<` `==` | |
| 1, but grouped last | `!=` `<>` | **Bind looser than `AND`/`OR`** when mixed with them ([E2](#e2)) |
| 0 | `AND` `OR` | Equal precedence, left to right, short-circuit (the right side is not evaluated when the left decides). |
| — | `<=` `>=` | **Silently dropped** by the compiler ([E1](#e1)). Never use them. |

Operators of one class are evaluated strictly left to right. Comparisons yield 0 or 1.

**Short-circuiting matters for `_SignalReceived`**, which consumes the signal: in
`x AND _SignalReceived(3)` the signal is only consumed when `x` is true.

### 5.3 Built-in function calls

```
_PlayerInRange(25)
_EnemyInRange(post, 40)
_AllPlayersDead()
```

- Parentheses are always required, even with no arguments.
- Each argument must be **a single variable or literal of the declared type**; no
  sub-expressions (`_RandomInt(n + 1)` fails to compile) ([E9](#e9)).
- Commas are optional (the compiler ignores them inside expressions).
- A `-` before a literal argument is allowed.

### 5.4 NOT

`NOT` and `!` negate **the next operand or function call** only: `!a == 0` means
`(!a) == 0`. Before a parenthesis they are **ignored**: `NOT (a == 5)` is the same as
`(a == 5)` ([E3](#e3)). Shipped scripts write `(!flag)`, which works.

### 5.5 Negative numbers

The sign handling is the weakest part of the language ([E4](#e4), [E5](#e5)):

| Written | Result | Correct? |
|---|---|---|
| `x = -5;` | -5 | yes |
| `x = 5 * -1;` | -5 | yes |
| `x = (-5);`, `x == (-1)` | as written | yes (parentheses always work) |
| `x = -5 + 2;` | **-7** | no |
| `x = a + -1;` | **a + 1** | no |
| `x = a - -1;` | **a - 1** | no |
| `x == -1`, `x < -60` | **x** (the comparison disappears; the port crashed here before [P2](#p2) was fixed) | no |
| `x = -5 * 2;` | **-5** (the `* 2` disappears) | no |
| `x = a - b;` (minus before a variable) | **compile error** | — |
| `x = a - (b);` or `a - 1 * b` | a - b | yes |

**Rule of thumb:** wrap every negative literal in parentheses, write `0 - 60` for
constants, and write `a - (b)` when subtracting a variable.

### 5.6 String expressions

When the target of an assignment is a String, the expression is converted to text:
operands are concatenated where they are joined by `+`; Integer operands are printed in
decimal (`"cp" + n` → `"cp3"`).

- Any other operator **drops its right-hand side**: `"x" + n * 2` gives `"x3"` for n = 3,
  `"a" - "b"` gives `"a"`.
- Function calls produce **nothing**: `"v" + _RandomInt(5)` gives `"v"`.
- Strings **cannot be compared**: `s == "abc"` compares 0 with 0 and is always true.

### 5.7 Writing safe expressions

The shipped scripts avoid every expression bug (none of 4,793 referenced scripts triggers
[E1](#e1)–[E5](#e5)); they do it by parenthesising everything. Do the same:

1. Parenthesise every comparison that is combined with `AND` / `OR`:
   `(a == 1) AND (b != 2)`.
2. Never use `<=` / `>=`: write `(a < 6)` instead of `a <= 5`, or `NOT`-free equivalents.
3. Wrap negative literals: `x == (-1)`.
4. Subtract variables as `a - (b)`.
5. Put `NOT` directly on an operand: `(!flag)`, never `NOT ( ... )`.
6. Compute function arguments into variables first.

---

## 6. Execution model

### 6.1 Scheduling

`C_ScriptModule::Tick` ([script_module.cpp:498](../src/script/script_module.cpp#L498))
runs once per game tick on the server and ticks every script (newest first, which is
roughly `scripts.dta` order with run-time assignments in front). During a cutscene only
scripts that are inside an `OnCutscene` handler are ticked.

Each script's `Tick` ([script.cpp:244](../src/script/script.cpp#L244)) does, in order:

1. If a `FRM_SetAnimationDelayed` is pending, check whether its animation ended.
2. Return if the script is **finished** (`EndScript`, `Return` with an empty stack, death
   without `OnDeath`).
3. Run the Whenever scheduler ([6.8](#68-whenever-scheduling)).
4. If a delay is pending, count it down; when it expires, move to the next command.
5. Otherwise, if the script is *running*, execute **one** command.

### 6.2 One command per tick

The interpreter executes at most one command per tick and then yields. Consequently:

| Construct | Cost |
|---|---|
| Declaration without initializer, `Label`, comment | nothing (no command) |
| Each command, assignment, initializer | 1 tick |
| `{` and `}` | 1 tick each |
| `If` | 1 tick (the branch starts on the next tick) |
| `GoTo`, `Gosub`, `Return` | 1 tick |
| Passing a handler or `Whenever` declaration in normal flow | 1 tick (its body is skipped) |
| `Block { ... }` | 1 tick for the whole body |

A loop such as `Label L: GoTo L;` therefore never hangs the game; it just uses one command
per tick.

One more guard: a command normally advances with `Advance()`, which refuses to move while
the mission clock is still 0 (before the mission's first tick), except during a restart.

### 6.3 Waiting

| Mechanism | How the script continues |
|---|---|
| `Delay(ms)` | Countdown in game time; the next command runs when it reaches 0. **`Delay(0)` never continues** ([R1](#r1)). |
| Human actions (`HUMAN_Move`, `HUMAN_Attack`, `HUMAN_MoveToFrame`, `HUMAN_Drive`, `HUMAN_MoveToAlarm`, ...) | The command passes its own index to the actor; the actor calls back when done and the script advances **only if it is still at that command**. |
| Activities (`HUMAN_ACTIVITY_*`, `HUMAN_Investigate`) | The activity's animation length is used as a delay. A human that is not under its scripted controller returns 0, which stalls like `Delay(0)` ([R1](#r1)). |
| `HUMAN_Suspend(true)` / `DOG_Suspend(true)` | Waits 2 s, then suspends and continues. |
| `FRM_SetAnimationDelayed` | Waits until the animation on **stage 0** ends ([R14](#r14)). |
| `FRM_MorphSpeechDelayed` | Waits for the length of the dubbing sample. |
| `DOG_Move`, `DOG_Attack` | Callback from the dog AI. |

Events and Whenevers move the instruction pointer away from a waiting command; the
completion callback is then ignored.

### 6.4 Ending, idling, restarting

| Situation | Effect |
|---|---|
| Past the last command | The script **idles**: not finished, still reacts to events and Whenevers. |
| `EndScript()` | **Finished**: no more commands or Whenevers. An AI human is released (optimisation off, unsuspended, AI reset); a dog's AI is reset. Only `OnDeath`, `OnAlarmDone`, `OnCutscene`, `OnCutsceneDone` can bring it back ([R9](#r9)). |
| `Return` with an empty stack | As `EndScript` ([R6](#r6)). |
| Actor dies without `OnDeath` | As `EndScript`. |
| `SetNPCTeamStatus(npc, true)` | The *calling* script finishes ([K5](#k5)). |
| Mission restart | Every script runs `OnExit`, is destroyed and recompiled; `SaveGameValue` slots are restored to their value at mission start. |

### 6.5 Event interplay

All events simply overwrite the instruction pointer. Specific interactions:

- `SendSignal`, `OnUse` and `OnHit` are ignored only when they arrive in the very tick an
  `OnDeath` or `OnAlarm` handler starts. `OnAlarm` is ignored only in the tick `OnDeath`
  starts. After that first tick, everything can interrupt them ([R9](#r9)).
- Whenevers and cutscenes can interrupt anything, including `OnDeath`.
- Events received while the script is `Delay`ing: most cancel the delay; `OnUse`, `OnHit`
  and `OnGlobalAlarm` do not, so their handler starts only when the delay runs out
  ([R9](#r9)).

### 6.6 Variables across missions

`SaveGameValue(slot, value)` and `_LoadGameValue(slot)` share 256 integer slots owned by
the script module. They survive from mission to mission. At mission start they are
cleared when the campaign-state field the port calls `campaign` is 1; otherwise they are
snapshotted so that a mission restart can restore them.

### 6.7 Signals

```
SendSignal(other_frame, 3);   // in script A
OnSignal(3) { ... }           // in the script of other_frame
```

- A signal is a number, normally 1..31. If the receiving script has an `OnSignal(n)`
  handler, the script jumps there (cancelling its `Delay`). Otherwise the signal is
  **remembered** as bit `n & 31` (only for n < 32) until `_SignalReceived(n)` polls and
  clears it.
- `DisableSignals(true)` ignores all signals and clears the remembered ones.
  `EnableSignal(n, false)` / `EnableSignal(n, true)` mask single signals (by `n & 31`).
- If the receiving frame is a human that is **not** under its scripted controller (e.g. a
  squad member the player controls), a signal with a handler is **dropped entirely** — not
  even remembered ([R11](#r11)).
- `SendSignal` to a frame without a script does nothing; to a **null frame** it stops a
  generic sender script ([R7](#r7)).
- Finished scripts are not woken by signals.

### 6.8 Whenever scheduling

The scheduler ([script.cpp:271](../src/script/script.cpp#L271)) accumulates game time and
works in **500 ms steps**:

- In each step every enabled Whenever (last declared first) adds 500 ms to its own
  counter; when the counter exceeds its period, the period is subtracted and the condition
  is evaluated.
- With the default 500 ms period a condition is first tested about **1 s** after the script
  starts, then every 500 ms. A period P is tested on average every P ms, quantised to
  500 ms steps, with the first test one step late. Periods below 500 ms behave like 500 ms.
  A re-armed Whenever continues from its old counter, so with the default period it is
  tested again in the next step.
- Only one Whenever can fire per step; Whenevers declared earlier in the file are not
  advanced in a step in which a later one fired.
- A very long tick (over 1 s, e.g. after loading) runs several steps at once; a second
  Whenever firing in a later step overrides the first one's jump, and the first is still
  marked disabled ([R10](#r10)).
- A Whenever's counter is kept while it is disabled.

### 6.9 Cutscenes

1. `CSC_RunCutscene(n)` gives every script with an `OnCutscene(n)` handler a jump there and
   marks it "in cutscene". Humans whose scripts have no such handler are deactivated
   (except player-team members). Player input is disabled, the panel closed, the view
   letterboxed to 16:9 and the camera's aspect / FOV switched to the cutscene values.
2. While the cutscene runs, **only scripts in their cutscene handler tick**. A script that
   started the cutscene without handling it is frozen until the end.
3. `CSC_EndCutscene()` restores the camera, gives `OnCutsceneDone(n)` handlers a jump,
   reactivates the humans that were stopped, re-shows the HUD where a script camera had
   hidden it, removes `CSC_DuplicateHuman` copies and animations started during the
   cutscene, issues a zero-length fade, sets the camera's aspect to 4:3 ([R16](#r16)), and
   gives input back after 500 ms.

If **no** script handles `OnCutscene(n)`, nothing will ever call `CSC_EndCutscene` and the
game stays in the cutscene ([R15](#r15)).

### 6.10 Saved games

What a script saves ([`SaveGame`](../src/script/script.cpp#L592)): instruction pointer,
Whenever timer, delay, signal masks, run / finished / disable flags, HUD flags, **all
Integer variables (including literal slots)**, Frame variables (by frame name), the
`Gosub` stack, a pending delayed animation and each Whenever's period / counter / enabled
flag. **String variables are not saved** ([R13](#r13)). On loading, the module recompiles
each saved script with `AssignScript` (0x004257b0) and restores this state on top.

### 6.11 Network play

Scripts run on the server only. Commands with client-visible effects send a script network
message:

| Message | Sent by |
|---|---|
| 0x5F frame on/off | `FRM_SetOn` |
| 0x6C speech | `FRM_MorphSpeech`, `FRM_MorphSpeechDelayed` |
| 0x7E light map | `FRM_SetLightMap` |
| 0x7F particle | `FRM_CreateParticle` |
| 0x83 light map group | `FRM_SetGroupLightMap` |
| 0xB8–0xC3 weather | `WEATHER_*` (except `WEATHER_Debug`), `THUNDERSTORM_MP` |
| 0xFA / 0xFB subtitles | `SUBTITLES_SetOn`, `SUBTITLES_SetText` |

`FRM_SetOn` does not switch the frame on the server itself; the clients (including the
local one) do. Several fields are only 8 bits wide ([K9](#k9)).

---

## 7. Built-in functions

All return integers. Distances are in metres. "Script frame" means the frame the script is
attached to; functions marked † dereference it without a check and **crash in a script
without a frame** ([K1](#k1)).

| Function | Args | Returns | Notes |
|---|---|---|---|
| `_PlayerInRange` | I range | **number** of players within range of the script frame | Players: living members of the player team; in co-op, of the co-op team (in play, not spectating). 0 without a script frame. |
| `_AllPlayersInRange` | I range | 1 if every living player-team member is within range (and one lives) | |
| `_PlayerInBox` | F dummy | 1 if a living, playing human in a player slot is strictly inside the dummy's box (world-aligned bounds of its rotated box) | 0 if the frame is not a dummy; **crash if null** ([K1](#k1)). |
| `_EnemyInRange` | F ref, I range | 1 if a living, conscious enemy-role human is within range of `ref` | 0 if `ref` is null. |
| `_MineInRange` | I range | 1 if a mine is within range of the script frame | |
| `_ItemInRange` † | I itemId, I range | 1 if a loose item of that type is in range | |
| `_FrameInRange` † | F target | distance from target to the script frame | 1000 if `target` is null; in network games also for dead / inactive human players. |
| `_CameraInRange` † | — | distance from the active camera to the script frame | 1000 without a camera. |
| `_GetDistToActivePlayer` † | — | distance to the player's active soldier | **INT_MIN without an active player** ([K2](#k2)). |
| `_AllPlayersDead` | — | 1 if no player-team member lives | |
| `_AllEnemiesDead` | — | 1 if no enemy lives | |
| `_GetNumLiveEnemies` | — | live enemy count | |
| `_GetCountOfCarnageEnemies` | — | enemy count for the carnage objective | |
| `_SignalReceived` | I n | 1 if signal n was remembered; **clears it** | See [6.7](#67-signals). |
| `_IsLanguageVersion` | S language | 1 if the game runs in that language | |
| `_RandomInt` | I max | 0 … max-1 (`rand() % max`) | 0 for max 0; a negative max behaves like its absolute value. |
| `_LoadGameValue` | I slot | `SaveGameValue` slot 0–255 | 0 out of range. |
| `_SeeEnemy`, `_SeeFriend` | — | 1 if the script's human sees an enemy / a friend | 0 for non-humans. |
| `_ACTOR_GetState` | F actor | actor state ([8.9](#89-world-actors)) | Human: 1 alive and conscious. Car: 1 not destroyed. Animation: 1 running. Wire cut: 1 cut. Door, radio, switch, photo place, static weapon, explosive / document place, pike, TS explosion: their state. 0 otherwise. |
| `_GetObjectiveStatus` | I game, I campaign, I mission, I objective | stored objective status | Any mission's objectives, from the profile. |
| `_GetAlarmType` | — | last alarm type of the script's human / dog ([8.15](#815-alarm-type-flags)) | |
| `_IsInInventory` | I itemId | 1 if the player team carries that item | |
| `_GetNumMembersOfClientTeam` | — | living player-team members | |
| `_GetHumanStoreID` | F human | the human's soldier-profile index | -1 for non-humans. |
| `_IsTeamMemberDead` | — | 1 if any player-team member is dead | |
| `_MP_AllTeamMembersAreInRange` † | I team (0/1), I range | 1 if every playing member of the team is in range | **0 as soon as any dead but connected player exists, on any team** ([K4](#k4)). |
| `_MP_IsTeamMemberInRange` † | I team (0/1), I range | 1 if a playing member of the team is in range | Same flaw, order dependent ([K4](#k4)). |
| `_MP_IsItemInTeamInventory` | I team, I itemId, F out | 1 if a playing team member carries the item | Writes that member's frame into `out` (null otherwise). |
| `_SPGetGameType` | — | `mission + 4 × campaign` field of the campaign state | -1 outside 0..10. |
| `_SPGetGameDifficulty` | — | difficulty | |
| `_GetCameraState` | — | 0 normal, 1 underwater | |
| `_SignalPistolFired` | — | 1 if a player fired the signal pistol | |

---

## 8. Commands

Columns: opcode, signature, behaviour, uses in shipped scripts. Unless stated otherwise a
command with an unusable argument (null frame, wrong frame type) does nothing and the
script moves on. Commands marked **waits** do not advance until something completes
([6.3](#63-waiting)).

### 8.1 Script control

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 1 | `ScriptAssign(F target, S script)` | Runs `Scripts\<mission>\<script>` on `target`. An empty name removes target's script (its `OnExit` runs). If target **already has a script, it is only removed** — call again to start the new one. A null target on a spawn-point script means the spawned actor. **Never use it on the script's own frame** ([R12](#r12)). | 164 |
| 135 | `SendSignal(F target, I signal)` | [6.7](#67-signals). Null target stops a generic script ([R7](#r7)). | 5425 |
| 249 | `EndScript()` | [6.4](#64-ending-idling-restarting). A Junkers88 gunner that is the active player gives control back. | 4934 |
| 39 | `Delay(I ms)` | **Waits** `ms` of game time. `Delay(0)` never returns ([R1](#r1)); ignored inside `Block` ([R2](#r2)). | 8526 |
| 139 | `SetWhenever(name, I on)` | Enables / disables a Whenever. Literal second argument. | 1794 |
| 142 | `SetWheneverTime(name, I ms)` | Sets its period. Literal second argument; no forward reference. | 285 |
| 150 | `DisableSignals(I on)` | Ignore all signals (and forget remembered ones). | 1951 |
| 153 | `EnableSignal(I signal, I on)` | Unmask / mask one signal. | 324 |
| 151 | `DisableAlarms(I on)` | Suppress `OnAlarm`. | 9 |
| 152 | `DisableWhenevers(I on)` | Stop all Whenever polling. | 1461 |

### 8.2 Mission, interface and debugging

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 136 | `SetObjectiveStatus(I objective, I status)` | Objective numbers start at 1. Status: **0** in progress, **1** completed (single player: +1000 experience for every living squad member, more or none depending on objective flags), **2** failed, **3** failed and **mission lost** (remaining open objectives become 2), **4** hidden (ignored by the completion check). When every non-hidden objective is non-zero the mission ends (success text if the last status set was 1). The original's copy-protection check could turn a status into 3 at random (0x0069bd30); the port drops it. | 500 |
| 137 | `AddDiaryText(I textId)` | Adds a diary entry. | 114 |
| 138 | `AddDebriefingText(I textId)` | Adds a debriefing line. | 0 |
| 141 | `SaveGameValue(I slot, I value)` | Slots 0–255, kept across missions ([6.6](#66-variables-across-missions)). | 1183 |
| 144 | `SetPlayerInput(I on)` | Enables / disables player control. | 10 |
| 147 | `PlayAvi(S file)` | Plays a video. | 3 |
| 158 | `SetInterfaceTimer(I ms)` | Shows a countdown in the HUD. | 9 |
| 250 | `SUBTITLES_SetOn(I on)` | Subtitles on / off (networked). | 987 |
| 251 | `SUBTITLES_SetText(I textId)` | Shows a subtitle (networked). | 373 |
| 255 | `HINT_ClearAll()` | Clears the hint list. | 14 |
| 256 | `HINT_Add(I hintId)` | Adds a hint. | 70 |
| 252 | `Mission_SetUniformDisguise(I on)` | Allows disguise by enemy uniform. | 8 |
| 253 | `SetupTacticalMode(I on)` | Enables / disables the tactical map mode. | 11 |
| 260 | `MP_EnableSpawnZone(S zone, I side)` | Enables a spawn zone for side 0 axis / 1 allied / 2 neutral. Network games only. | 23 |
| 180 | `CheckpointSeparate(S a, S b)` | Removes the link between two checkpoints (walking network; road network if that fails). | 18 |
| 181 | `CheckpointJoin(S a, S b)` | Adds the link. | 27 |
| 259 | `AddPauseSound(S frameName)` | That sound frame is switched off while the game is paused. | 2 |
| 140 | `Printf(S text)` | Debug print; empty in the release build. | 1902 |
| 183 | `PrintfNumber(S text, I value)` | Debug print of `text value`; empty in the release build. | 1529 |
| 110–112 | `__Log(S)`, `__LogNumber(S, I)`, `__LogFrame(S, F)` | Stripped. In human / dog scripts they **skip the next command** ([R8](#r8)). | 0 |
| 105 | `FRM_TimerSetOn(I on, I ms)` | Stripped, same skip ([R8](#r8)). | 0 |
| 113 | `__SetupAutoLogging(I on, S file)` | Does nothing. | 12 |

### 8.3 Finding and changing frames

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 94 | `FRM_FindFrame(F out, S name)` | `out` = the named frame (engine lookup, then scene search) or null. | 13673 |
| 102 | `FRM_FindChildFrame(F out, S name, F parent)` | Searches below `parent`. | 54 |
| 120 | `FRM_GetMyFrame(F out)` | `out` = the script's frame. | 1942 |
| 69 | `FRM_GetNearestPlayer(F ref, F out)` | Nearest living, playing player-team (co-op team) soldier to `ref`, or null. | 14 |
| 95 | `FRM_SetOn(F frame, I on)` | Shows / hides a frame on all clients. | 4447 |
| 100 | `FRM_TeleportNearCheckpoint(F frame, S point, S dirPoint)` | Moves a frame or actor to a checkpoint, facing `dirPoint` (empty: keep direction). Cars use the road network and are reset. | 85 |
| 104 | `FRM_TeleportNearFrame(F moving, F target, I link)` | Puts `moving` at `target`'s origin; with `link`, parents it to `target`, otherwise to the sector there. Actors take the target's direction. The old state is remembered. | 298 |
| 109 | `FRM_TeleportNearFrameBack(F frame)` | Restores what the previous command remembered. | 9 |
| 106 | `FRM_SetTarget(F frame, F target, I link)` | Adds (`link` true) or removes `frame` to / from an engine *target* frame (`FRAME_TARGET`). | 9 |
| 107 | `FRM_CreatePhysicalObject(F frame, I kg, I fadeOut, I moveCollision)` | Turns the frame into a physics debris object (initial upward velocity 1 m/s). | 67 |
| 116–118 | `FRM_RotateX/Y/Z(F frame, I degrees, I degreesPerSecond)` | Rotates over time (asynchronous; a new call for the same frame replaces it). Undone at mission stop. | 63 / 23 / 7 |
| 122 | `FRM_WatchTrack(F frame, S track)` | Plays a recorded track on the frame. On a train, follows that track (empty name stops it). | 35 |
| 133 | `FRM_WatchTrackWithParent(F frame, S track, F parent)` | Track relative to `parent`. | 1 |
| 174 | `FRM_SetShifting(F frame, I on, I reverse)` | Texture-shift (scrolling) animation of a visual. | 8 |
| 58 | `FRM_SwitchFaceTexture(F model, S texture)` | Human: sets its texture; other models: swaps the face texture. | 1660 |
| 114 | `ITEM_Teleport(F item, F target)` | Moves an item actor to `target`. | 19 |
| 119, 121 | `FRM_Move()`, `FRM_GetPosition()` | Keywords with no table entry and no implementation; they **stop generic scripts** ([R7](#r7)). | 0 |

### 8.4 Animation

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 93 | `FRM_SetAnimation(F model, S anim, I stage)` | Loops `anim` on animation stage 0–7 of a model. A name shorter than 2 characters or not loadable clears the stage. | 472 |
| 96 | `FRM_SetAnimationDelayed(F model, S anim, I stage)` | Plays once and **waits** until stage **0** finishes ([R14](#r14)). | 1 |
| 97 | `FRM_ResetAnimation(F model, I stage)` | Rewinds and removes the stage's animation. | 20 |
| 99 | `FRM_StopAnimation(F model, I stage)` | Stops the stage. A null model stops a generic script ([R7](#r7)). | 6 |
| 167 | `CSC_SetCameraAnimation(F camera, S anim)` | Plays a camera animation in world coordinates. | 345 |

Animation names starting with `%` refer to the shipped `MODELS\%name.5ds` files; in scripts
they must be written with `%%` ([P1](#p1)). Names starting with `#` are also common.

### 8.5 Sound, music and speech

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 134 | `PlaySound(I chapter, I index)` | Plays a sound-table entry at the script frame (silent to the AI). Nothing without a frame. | 240 |
| 143 | `PlayMusic(I index)` | Index into the `MUSIC` chapter. | 71 |
| 156 | `PlayCutsceneMusic(I index)` | Crossfades (4 s) to a track; -1 returns to the mission music (alarm music during an alarm). | 16 |
| 101 | `FRM_SoundAdjustVolume(F sound, I percent)` | 100 = 1.0. Undone at mission stop. | 0 |
| 103 | `FRM_SoundAdjustDoppler(F sound, I x, I y, I z)` | Sound velocity in m/s. | 13 |
| 108 | `FRM_MorphSpeech(F model, I phrase, I near, I far)` | Lip-synced speech of a dubbing phrase, audible from `near` to `far` metres (0–255). | 679 |
| 115 | `FRM_MorphSpeechDelayed(F model, I phrase, I near, I far)` | As above and **waits** for the phrase to end. | 1505 |

### 8.6 Lights, particles, explosions

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 126 | `FRM_SetLightMap(F visual, I level)` | Selects light map level of a lit object. A non-visual frame **stops the script** ([R7](#r7)). | 381 |
| 131 | `FRM_SetGroupLightMap(S group, I level)` | Same for a light map group. | 190 |
| 127 | `FRM_CreateParticle(I id, F at)` | One-shot effect on the clients (id 0–255). | 323 |
| 128 | `FRM_CreateIndexedParticle(I id, F at, I out)` | Server-side effect; writes its handle into the Integer variable `out`. | 106 |
| 129 | `FRM_DestroyIndexedParticle(I handle)` | Removes it at once (-1 ignored). | 91 |
| 130 | `FRM_FinishIndexedParticle(I handle)` | Lets it run out (-1 ignored). | 11 |
| 146 | `MakeExplosion(F at, I damage, I range)` | Damage in thousandths (1000 = 1.0), range in millimetres. | 273 |

### 8.7 Cameras

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 123 | `FRM_SetActiveCamera(F camera)` | Switches the scene camera (copying range, aspect and FOV). A non-game camera hides HUD and panel until a game camera returns or the cutscene ends ([R17](#r17)). | 171 |
| 124 | `FRM_GetActiveCamera(F out)` | `out` = the active camera. | 18 |
| 125 | `FRM_SetCameraFrame(F frame)` | The game camera follows `frame`. | 45 |
| 132 | `FRM_SetCameraFrameSlide(F frame, I speed)` | Same, sliding at `speed` degrees. | 13 |
| 178 | `CSC_SetCameraFOV(I degrees)` | Field of view of the active camera (crash without one). | 3 |

### 8.8 Cutscenes

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 162 | `CSC_RunCutscene(I n)` | [6.9](#69-cutscenes). | 39 |
| 163 | `CSC_EndCutscene()` | [6.9](#69-cutscenes). Nothing if no cutscene runs. | 48 |
| 177 | `CSC_SetupFakeCutscene(I on)` | Sets the module's fake-cutscene flag. | 5 |
| 159 | `CSC_SetTimer(I speed, I acceleration)` | Game speed, 1000 = normal ("zotti"). | 4 |
| 160 / 161 | `CSC_FadeIn(I ms)` / `CSC_FadeOut(I ms)` | Screen fade. | 113 / 91 |
| 165 / 166 | `CSC_FadeInFromColor(I ms, I r, I g, I b)` / `CSC_FadeOutToColor(...)` | Coloured fade, 0–255 (clamped). | 1 / 1 |
| 168 | `CSC_DuplicateHuman(F source, F out, S name)` | During a cutscene only: a model copy of `source` (weapon moved to the right hand), named `name` if that is unique. Removed at the end of the cutscene. | 22 |
| 169 | `CSC_ReleaseHuman(F copy)` | During a cutscene only: removes a copy now. | 9 |
| 170 | `CSC_GetPlayerNum(I index, F out)` | `out` = the index-th living player-team member (null if none). | 30 |
| 179 | `CSC_GetPlayerNumNoNPC(I index, F out)` | Same, skipping NPC team members. | 0 |
| 171 | `CSC_FreeHands(F model)` | Detaches the weapons in the hand joints of a model (use on copies). | 12 |
| 172 | `CSC_FreeBackPack(F model)` | Hides the visuals on the backpack joint. | 3 |
| 175 | `CSC_FrameSetShadow(F model, I on)` | Creates / removes a model's shadow. | 1 |
| 176 | `CSC_NoTeamDamage(I on)` | Disables team damage. | 8 |
| 173 | `CSC_RunCLouder(I on)` | Cloud effect on / off. | 5 |

### 8.9 World actors

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 149 | `SetActorState(F actor, I state)` | See the table below. | 426 |
| 154 | `DOOR_Lock(F door, I lock)` | Locks / unlocks with the door's own key. | 95 |
| 155 | `DECORATOR_SetOn(I on)` | Decorator objects on / off. | 2 |
| 157 | `AirAttack_SetTarget(F target, I attack)` | The **script's own frame** must be an air-attack actor. | 23 |
| 98 | `Car_SetFuel(F car, I tenthsOfLitre)` | Sets the fuel. | 28 |
| 145 | `SetNPCTeamStatus(F npc, I status)` | With `status` true an allied NPC joins the player's team; **the calling script ends** in any case ([K5](#k5)). False does nothing. | 15 |
| 148 | `SetAlarmType(I flags, I on)` | Enables / disables alarm causes for the script's human ([8.15](#815-alarm-type-flags)). | 11526 |
| 50 | `HUMAN_Kill(F actor)` | Kills a human (script-kill hit); stops an animation actor. | 143 |
| 55 | `HUMAN_KillEx(F human, I mode)` | Kills with the alternative script-kill hit; `mode` is ignored. | 9 |
| 60 | `HUMAN_KillAllInBox(F dummy)` | Every playing human inside the dummy's box gets an explosion hit — **players included** ([K10](#k10)). A non-dummy frame skips the next command in actor scripts ([R8](#r8)). | 11 |
| 254 | `HUMAN_RemoveItemsFromStore(F human, I itemId)` | Removes items of that type. | 38 |
| 257 | `AddHumanHealth(F human, I percent)` | Adds health percent. | 4 |
| 258 | `SetupJunkers88(I firepower%, I hp%)` | Interceptor strength in the Junkers88 sequence. | 1 |

`SetActorState` / `_ACTOR_GetState`:

| Actor | `SetActorState(actor, state)` | `_ACTOR_GetState(actor)` |
|---|---|---|
| Door | non-zero opens, 0 closes (animated) | door state |
| Radio, switch, static weapon, explosive place, document place, Junkers88 | sets the state | the state (not Junkers88) |
| Supply box | enabled = state ≠ 0 | 0 |
| Train | starts (state ignored) | 0 |
| Animation | 666: stop at the end of the cycle; other: stop now | 1 while running |
| Pike | non-zero up, 0 down | state |
| Fowl generator | starts (state ignored) | 0 |
| Wire cut | non-zero cuts along the frame's direction | 1 if cut |
| TS explosion | triggers (state ignored) | state |
| Human | — | 1 alive and conscious |
| Car | — | 1 not destroyed |
| Photo place | — | state |

### 8.10 Formations

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 2 | `FORMATION_Create(F leader)` | Creates the leader's formation. | 18 |
| 3 | `FORMATION_Destroy(F leader)` | | 11 |
| 4 | `FORMATION_AddMember(F member, F leader)` | Moves `member` into the leader's formation. | 45 |
| 5 | `FORMATION_DelMember(F member)` | | 67 |
| 6–11 | `FORMATION_SetTypeColumn / Line / Quad2X3 / Quad3X2 / ArrowSharp / ArrowInvert(F leader)` | Formation shape. | 6 / 6 / 6 / 0 / 4 / 2 |
| 12 | `FORMATION_SetScale(F leader, I cm)` | Spacing. | 14 |

### 8.11 Weather

Sent to the clients and applied to the server's scene (except where noted).

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 184 | `WEATHER_SetOn(I on)` | Rain / snow on or off. | 10 |
| 193 | `WEATHER_SetMode(I mode)` | 0 rain, 1 snow (clients only learn "snow or not", [K8](#k8)). | 5 |
| 185 | `WEATHER_SetCount(I n)` | Number of particles. | 14 |
| 186 | `WEATHER_SetSpeed(I mmPerSecond)` | Clamped to 0–200 m/s. | 13 |
| 189 | `WEATHER_SetSize(I lengthMm, I widthMm)` | Clamped to 0.0001–20 m and 0–500 m. | 11 |
| 190 / 191 | `WEATHER_SetColorIn / SetColorOut(I a, I b, I g, I r)` | Colour, packed with `a` in the low byte. | 11 / 11 |
| 194 | `WEATHER_SetSpace(I distance, I height)` | Volume around the camera, in metres. | 4 |
| 195 | `WEATHER_SetDir(I x, I y, I z)` | Fall direction (normalised). | 6 |
| 192 | `WEATHER_Debug(I on)` | Local only. | 0 |
| 187 | `THUNDERSTORM_SetOn(I on, F light)` | Lightning, optionally flashing a light frame. **Local only** ([K8](#k8)). | 2 |
| 188 | `THUNDERSTORM_MP(I time)` | Network thunderstorm message only; skips the next command in actor scripts ([R8](#r8)). | 1 |

### 8.12 Human commands

Only in scripts of humans ([1.3](#13-script-kinds)); they act on the script's own human.
"waits" = [6.3](#63-waiting).

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 31 | `HUMAN_Move(S checkpoint)` | **Waits** until arrived. Unknown checkpoint: continues. | 6516 |
| 32 | `HUMAN_AttackMove(S checkpoint)` | Moves while fighting; **waits**. | 281 |
| 33 | `HUMAN_Attack(F target, I ms)` | Attacks an actor or a frame for `ms`; **waits**. | 366 |
| 48 | `HUMAN_IllFire(F target, I ms)` | Fake attack (bullets pass actors, silent to the AI); **waits**. | 164 |
| 51 | `HUMAN_MoveToFrame(F target, I m)` | To the checkpoint (within 5 m) nearest the point `m` metres from `target` on the human's side; **waits**. | 67 |
| 53 | `HUMAN_MoveFromFrame(F target, I m)` | `m` metres further away from `target`; **waits**. | 28 |
| 54 | `HUMAN_MoveRandomRadius(I m)` | To a random checkpoint reachable within `m`; **waits**. | 96 |
| 65 | `HUMAN_MoveToAlarm()` | To the alarm position; **waits**. | 81 |
| 38 | `HUMAN_Drive(S roadPoint, I kmh)` | Drives the car the human sits in; **waits**. Not in a car: continues. Unknown point: stops driving and continues. A human not under its scripted controller **stalls** ([R7](#r7)). | 459 |
| 43 | `HUMAN_DriveNearPlayer(I kmh)` | Driver only: to a random road point within 20 m of the nearest player; **waits**. | 5 |
| 182 | `HUMAN_Bike(S bike, S roadPoint, I speed)` | Rides a bike; empty point = just mount. With a point it **never continues**; unknown bike / point **stalls** ([K7](#k7)). | 0 |
| 52 | `HUMAN_BoardVehicle(S vehicleFrame, I on, I seat)` | Boards a car, static weapon or Junkers88 seat; off or empty name leaves. | 564 |
| 37 | `HUMAN_Stop()` | Stops the current action. | 1202 |
| 47 | `HUMAN_Suspend(I on)` | true: **waits** 2 s then suspends the AI; false: resumes. | 5509 |
| 63 | `HUMAN_SetOptimized(I mode)` | AI optimisation mode. | 3762 |
| 29 | `HUMAN_SetAlarm(I on)` | Sets / clears the human's alarm state. | 3001 |
| 46 | `HUMAN_SetIgnoreCircumventionFlag(I on)` | Path circumvention off / on. | 6 |
| 66 | `HUMAN_SetWaitForPlayer(I on, I stopM, I resumeM, I watch)` | Waits for the player when further than `stopM`, resumes within `resumeM`. | 4 |
| 68 | `HUMAN_SetSniper(I on, I radiusM)` | Sniper behaviour. | 214 |
| 56 | `HUMAN_SetAnim(S anim, I blendMs, I cancelMs, I loop)` | Plays a body animation; `""` stops it. | 3751 |
| 62 | `HUMAN_TurnAt(F target)` | | 2252 |
| 67 | `HUMAN_TurnAtNearestPlayer()` | | 919 |
| 64 | `HUMAN_LookAround(I type)` | Look-around pattern 0–6 (clamped). | 542 |
| 28 | `HUMAN_CreateItemInStore(I itemId, I storeType)` | Gives the human an item. | 11 |
| 36 | `HUMAN_Reload()` | | 151 |
| 213 | `HUMAN_WeaponOnArm(I on)` | Takes the weapon / holsters it. | 1392 |
| 215 | `HUMAN_WeaponUp(I on)` | Raises / lowers the weapon. | 62 |
| 214 | `HUMAN_PickObject(F object, I oneHand)` | One hand only when `oneHand` is exactly 1. | 4 |
| 216 | `HUMAN_DropObject()` | | 10 |
| 196–203 | `HUMAN_SETMODE_Run / Walk / Silence / Guard / Lie / Crouch / Stand / StandFast()` | Movement / stance modes. | 2094 / 965 / 155 / 845 / 103 / 2031 / 1308 / 68 |
| 205–207 | `HUMAN_SETAIMODE_Aggressive / Defensive / Zombie()` | AI mode (0x100 / 0x200 / 0x400). | 749 / 1114 / 846 |
| 208–210 | `FRAME_SETAIMODE_Aggressive / Defensive / Zombie(F human)` | The same for another human. | 48 / 18 / 0 |
| 211 | `HUMAN_SETSIGHTRANGE(I m)` | | 1480 |
| 212 | `HUMAN_SETHEARRANGE(I m)` | | 601 |
| 248 | `HUMAN_SETEVENTS(I on)` | Perception events on / off. | 811 |
| 44 | `HUMAN_Investigate(F target)` | Investigation activity; **waits**. | 7 |

Activities (`HUMAN_ACTIVITY_*`). Those that wait use the animation length as a delay.

| Op | Activity | Args | Behaviour | Uses |
|---|---|---|---|---|
| 218 | `Card` | I v | 1 and others: play cards (two variants), 0: stop; **waits** | 4 |
| 219 | `Cubes` | — | dice; **waits** | 0 |
| 224 / 225 / 226 | `CheckWeapon` / `Shoes` / `Hat` | — | **waits** | 5 / 0 / 13 |
| 228 / 229 | `Cold` / `Hot` | — | **waits** | 10 / 18 |
| 227 / 247 | `Binoculars` / `BinocularsEnd` | — | looping start / end; no wait | 5 / 7 |
| 234 | `Telegraph` | I on | on: telegraph, off: stop; **waits** | 0 |
| 235 / 236 | `Phone` / `Read` | I on, F at | on: at `at`, off: stop; **waits** | 0 / 0 |
| 237 | `DrinkBottle` | F bottle | with or without a bottle frame; **waits** | 29 |
| 238 | `Eat` | I on, F at | **waits** | 0 |
| 239 | `Turk` | — | no wait | 18 |
| 240 | `Salute` | I v | three variants (1, 2, other); **waits** | 45 |
| 244 | `Sit` | F seat | no wait | 76 |
| 246 | `Point` | F target | **waits** | 41 |
| 217 | `Smoke` | I | **not implemented** ([K6](#k6)) | 252 |
| 220–223 | `SpotUp / SpotDown / SpotRight / SpotLeft` | I | not implemented | 0 |
| 230–233 | `PlayCar / PlayAirplane / PlayPuppet` (I), `Trap` (F) | | not implemented | 0 |
| 241–243, 245 | `Exercise / StandSleep / Piss` (I), `Sleep` (F) | | not implemented | 0 |

### 8.13 Dog commands

Only in scripts of dogs.

| Op | Command | Behaviour | Uses |
|---|---|---|---|
| 13 | `DOG_SetOptimized(I on)` | | 0 |
| 14 | `DOG_Suspend(I on)` | Like `HUMAN_Suspend`. | 5 |
| 15 / 16 | `DOG_SetRangeSight(I m)` / `DOG_SetRangeHear(I m)` | | 5 / 6 |
| 17 | `DOG_ClearAlarm()` | | 5 |
| 18 | `DOG_SetOwner(F owner)` | | 5 |
| 19 | `DOG_Move(S checkpoint)` | **Waits** for the dog AI. | 9 |
| 20 | `DOG_Attack(F target)` | **Waits** for the dog AI. | 5 |
| 21–24 | `DOG_SETMODE_Run / Walk / Stand / Lie()` | | 8 / 0 / 1 / 1 |
| 25 | `DOG_HafHaf(I on)` | Barking on / off. | 0 |
| 26 / 27 | `DOG_SETAIMODE_Aggressive / Defensive()` | | 5 / 0 |

### 8.14 Commands that do nothing

Accepted by the compiler but not implemented in the release build (the script just moves
on in an actor script; generic scripts stop on actor commands, [R7](#r7)):

`HUMAN_Put`, `HUMAN_Open`, `HUMAN_Climb`, `HUMAN_UseBomb`, `HUMAN_Unlock`, `HUMAN_LookAt`,
`HUMAN_SETMODE_DontSeekBestPos`, the activities marked "not implemented" above (the
original's human command switch at 0x00421c00 has no case for any of these),
`__SetupAutoLogging`, and the stripped debug commands `__Log`, `__LogNumber`, `__LogFrame`,
`FRM_TimerSetOn` ([R8](#r8)).

### 8.15 Alarm type flags

`SetAlarmType(flags, on)` switches alarm causes on or off for the script's human;
`_GetAlarmType()` returns the cause of the last alarm. The meanings below come from the
designers' own comments in the shipped scripts (Czech / Slovak), which are consistent
across hundreds of files:

| Bit | Cause |
|---|---|
| 1 | Shooting nearby |
| 2 | Hears distant shooting |
| 4 | Footsteps |
| 8 | Noises from using objects |
| 16 | Explosion |
| 32 | Sees an enemy |
| 64 | Is wounded |
| 128 | Hears a death scream (some comments say "alarm from a script") |
| 256 | Friendly fire |
| 512 | Sees a dead body |

A typical setup enables everything with `SetAlarmType(1023, true)` and then switches off
unwanted causes, e.g. `SetAlarmType(652, false)` (footsteps, use noises, death scream,
bodies), which leaves the soldier reacting to shooting, explosions, seeing an enemy, being
wounded and friendly fire (371).

---

## 9. Compiled form

For porters and tool writers: what the compiler ([script_compiler.cpp](../src/script/script_compiler.cpp))
produces.

- **Commands**: `S_ScriptCommand { int opcode; int *args; }`. The argument count comes from
  the command table (`g_scriptCommandDefs`, original 0x008338a8), at most 4.
- **Tables**: numbers (`S_ScriptNumber {value, isConstant}`), strings (`C_str`), frames
  (`I3D_frame *`, each holding a reference), expressions, Whenevers
  (`{expression, label, enabled, period, elapsed}`), and handler lists.
- **Arguments** are table indices; `GoTo` / `Gosub` hold the target command index;
  `OnSignal` / `OnCutscene` / `OnCutsceneDone` hold the number itself; `If` / `Whenever`
  hold an expression index; `SetWhenever` / `SetWheneverTime` hold a Whenever index.
- **Blocks**: `{` is opcode 261 with one argument, the number of commands in its body;
  `}` is 262. Skipping a block means `ip = index_of_{ + body + 2`.
- **Assignments** are opcode 263 `(expression, targetType, targetIndex)`.
- **Forward references**: an unknown label or Whenever is encoded as `-1 - symbolIndex` and
  patched when it is defined; leftovers fail the compilation.
- **Handlers** record the index of the command after their opcode (their `{`).
- **Expressions** are trees: a node is a leaf (operand or function call with up to 4
  operand indices) or a list of children joined by operators, evaluated left to right.

Example — this source:

```
Integer x = 0;
Label top:
If (x == 0) { x = 1; } Else { x = 2; }
Whenever w (x == 1) { SetWhenever(w, 1); }
OnSignal(3) { GoTo top; }
```

compiles to:

```
[0] 263 Evaluate          x = 0                      (initializer)
[1]  78 If                expr 1
[2] 261 {                 body 1
[3] 263 Evaluate          x = 1
[4] 262 }
[5]  79 Else
[6] 261 {                 body 1
[7] 263 Evaluate          x = 2
[8] 262 }
[9]  80 Whenever          expr 4                     (label = 10)
[10] 261 {                body 1
[11] 139 SetWhenever      whenever 0, number 6
[12] 262 }
[13]  83 OnSignal         3                          (handler entry = 14)
[14] 261 {                body 1
[15]  77 GoTo             1
[16] 262 }
```

---

## 10. Bugs and quirks

**Origin**: *Port* = introduced by the C++ port (P1–P4 are fixed now; their entries are
kept as a record). *Original* = present in the shipped game
(confirmed in its code at the given address) and kept by the port. *Fixed* = an original
bug the port already fixes.

**Fix safety** (for original bugs): *safe* = no shipped, referenced script triggers it, so
fixing it cannot change shipped missions; *changes shipped behaviour* = shipped scripts
hit it, so fixing it alters the original's behaviour.

### 10.1 Summary

| ID | Severity | Origin | Summary | Shipped scripts affected |
|---|---|---|---|---|
| [P1](#p1) | **High** | Port, **fixed** | `%%` in string literals was no longer turned into `%`: ~2,400 animation names broke | 932 files |
| [P2](#p2) | **High** | Port, **fixed** | Negative literal after an operator crashed the compiler (`x == -1`) | 1 unreferenced file |
| [P3](#p3) | Low | Port, **fixed** | `#include` was expanded anywhere, even in comments | none |
| [P4](#p4) | Info | Port, **fixed** | Misleading comment about `SetWhenever` forward references | — |
| [C1](#c1)–[C15](#c15) | Low–Med | Original | Compiler | see each |
| [E1](#e1)–[E10](#e10) | Med | Original | Expressions | none referenced |
| [R1](#r1)–[R17](#r17) | Low–High | Original | Interpreter | see each |
| [K1](#k1)–[K10](#k10) | Low–Med | Original | Commands and functions | see each |
| [F1](#f1)–[F5](#f5) | — | Fixed | Original bugs the port fixes | — |
| [S1](#s1)–[S11](#s11) | — | Content | Errors in the shipped scripts | — |

### 10.2 Port bugs

#### P1
**`%%` was no longer turned into `%` in string literals — broke most animation names.**
*Port, high — **fixed**.* The original copied every string literal argument with
`sprintf(buffer, literal)` ([`ParseLiteral`](../src/script/script_compiler.cpp#L384),
original 0x0042d920), and string literals inside expressions with `C_fstr(literal)`
(original 0x00411d00). The designers knew this and wrote `%%` for a literal `%`. The port
had removed the formatting and passed `%%` through unchanged.

- Evidence: the shipped animations are files like `MODELS\%turnright.5ds` (1,253 model
  files start with `%`, none with `%%`), while scripts write `"%%turnright"`. The name
  reaches the animation code unchanged (`C_Human::SetAnimation` → `C_ScriptedController::SetAnimation`, 0x00591230).
- Impact before the fix: 2,516 lines in 932 shipped scripts, almost all of them
  `HUMAN_SetAnim` (≈2,140 calls), `FRM_SetAnimation` (253) and `CSC_SetCameraAnimation`
  (21). Every one of these animations would have failed to load in the port.
- Fix: both places now pass the literal through
  [`ScriptStringLiteral`](../src/script/script_compiler.cpp#L372), which applies MSVC's
  `printf` semantics with no arguments: `%%` → `%`; `%x` → `x` for any other character
  (MSVC prints a character that starts no conversion); and a `%` at the end is dropped.
  The last rule matters: `TUTORIAL\TUT_Instructor7FSH0.scr` writes `"%%nudazed1%"`, which
  the original turned into the existing animation `%nudazed1`. (`ParseLiteral` formatted
  the text together with its closing quote and then cut off the last character, so a
  trailing `%` vanished there too.) Real conversions (`%d`, `%s`) read missing arguments in
  the original. They do not occur in shipped scripts, and the port treats them like any
  other character. The one other `%` literal ([S10](#s10)) now also behaves as in the
  original.
- Verified: all 1,732 string-table entries containing `%` in the compiled shipped scripts
  now start with a single `%` (e.g. `"%%turnright"` → `%turnright`), and none contains
  `%%`.

#### P2
**A sign kept as an operator crashed the compiler.** *Port, high (crash) — **fixed**.*
`C_ScriptExpression::GroupRange` called `m_children.erase(begin() + first + 1)` when
`first + 1 == m_children.size()`, i.e. `erase(end())`, which is undefined behaviour and
crashed (reproduced: access violation in `memmove` from that line). It happens whenever a
negative literal follows an operator and the expression mixes precedence classes
([E5](#e5)): `x == -1`, `x < -60`, `x != -1`, `-5 * 2`.

- The original (0x00412560) stays in bounds by luck: it writes the new group into the last
  slot and then shortens the list, which drops the last operand, so `x == -1` evaluates to
  `x`. (When a second such operator comes later, it writes one slot past the end and
  again drops the last operand.)
- Shipped impact before the fix: one file, `TUTORIAL\T_EE_Weather.scr` line 69
  (`If ( X <  -60 )`), which is not referenced by any `scripts.dta`; compiling it crashed
  the port. Mods that compare against negative numbers would have crashed the server at
  mission start.
- Fix (faithful minimum): [`GroupRange`](../src/script/script_expression.cpp#L225) now
  checks for a missing right operand first and reproduces the original's result in
  bounds: it deletes the last operand and removes the operator. `x == -1` → `x`,
  `-5 * 2` → -5 and `-5 * -2 * -3` → -5, as in the original, and `T_EE_Weather.scr`
  compiles. The sign handling itself ([E5](#e5)) is left as it was. A proper fix (fold
  the sign into the literal and drop the `-` token) would be safe for shipped content,
  but it changes documented results such as `-5 + 2` = -7 and also touches the
  function-argument path, so it remains a separate, optional change.

#### P3
**`#include` was expanded anywhere, including inside comments and strings.** *Port, low —
**fixed**.* [`Preprocess`](../src/script/script_compiler.cpp#L164) searched the raw text,
so `// #include "old.scr"` still included the file, and the keyword was blanked inside
string literals. The original honoured only a directive at the top of the file
([C1](#c1)). The port now honours a directive only when nothing but spaces or tabs
precede it on its line, and leaves any other occurrence untouched. No shipped script is
affected (all 7 includes start line 1).

#### P4
**Misleading comment about `SetWhenever` forward references.** *Port, documentation —
**fixed**.* [`ParseWhenever`](../src/script/script_compiler.cpp#L693) said the original
"overwrote their second argument with an unrelated stack value". The original's
disassembly (0x0042ea45–0x0042eb70) stores `args[1]` in the stack slot right after the
patched `args[0]` and hands both to `SetArgs`, so the second argument is copied intact —
the original and the port behave the same. This matters because 476 referenced shipped
scripts use a forward `SetWhenever`. The comment now says so, so that nobody "restores"
the bug.

### 10.3 Original bugs — compiler

#### C1
**Only an `#include` at the very top of the file works.** Original 0x0042c580: the
directive is blanked but the file name is read by the tokenizer starting at the beginning
of the source. *Fixed in the port*: a directive at the start of any line works
([P3](#p3)).

#### C2
**String literals are pooled case-insensitively, including with String variables.**
`ParseLiteral` (0x0042d920) returns any existing string table entry that matches
case-insensitively. `"ABC"` and `"abc"` become one literal, and a `""` literal is
**aliased to any String variable declared earlier** (its slot holds `""` at compile time):
after `String s; s = "cp_north";`, a later `FRM_TeleportNearCheckpoint(f, "cp", "")`
receives `"cp_north"` as its third argument (reproduced). *Safe to fix*: no shipped
script declares a String.

#### C3
**`Whenever`, `OnHit` and `OnGlobalAlarm` need no block.** The end-of-compile check
(0x0042c850) verifies that `Else` and the other handlers are followed by `{` but not these
three. Without a block, the "skip the body" logic reads the next command's first argument
as a body length and jumps to an arbitrary command (or dereferences null arguments).
Shipped case: [S5](#s5).

#### C4
**Comments only between statements.** A comment token inside a statement is not
skipped: `If (x) // why` followed by `{` on the next line fails to compile, as does a
comment inside an argument list. A comment after `{` or `;` is fine.

#### C5
**`SetWheneverTime` cannot refer to a Whenever defined later** (only `SetWhenever`
forward references are resolved), so it fails the compilation.

#### C6
**`SetWhenever` / `SetWheneverTime` take only a literal second argument**
(`SetWhenever(w, on)` with a variable is a compile error).

#### C7
**`Real`, `Bool`, `Enum`.** `Real` is a keyword but not handled by the statement parser:
`Real x;` fails. `Bool` and `Enum` declare names without storage; using them reads 0 and
assignments are dropped silently.

#### C8
**An unknown name or token silently ends an expression.** `AddToken` rejects the token, the
compiler steps back one character and finishes the expression. Usually the leftover text
then fails as the next statement, but not always — e.g. `{X = 0 - 60}` compiles although
the `;` is missing (shipped in `T_EE_Weather.scr`).

#### C9
**Compile errors are silent.** `AssignScript` deletes a script that failed to compile and
reports nothing; the frame just has no script. The error line is available
(`GetLine()`) but unused. 42 shipped scripts fail to compile in the original as in the
port ([S1](#s1)–[S3](#s3)).

#### C10
**Opening parentheses are not checked** for `If`, `Whenever` and the numbered handlers:
`If x == 1) {` compiles.

#### C11
**Line numbers count only CR.** Files with LF line ends (e.g. the CASTLE1 scripts) report
every error on line 0.

#### C12
**Initializers above `ScriptStartPoint` never run** (they are ordinary commands before
the start point).

#### C13
**One namespace; duplicates ignored.** Variables and Whenever names share one namespace
(a Whenever may reuse a variable's name and hides it from then on). Of duplicate
handlers only one ever runs ([4.8](#48-event-handlers)).

#### C14
**Handler numbers cannot be negative** (`OnSignal(-1)` is a compile error, although
`SendSignal(f, -1)` is legal and sets bit 31).

#### C15
**Sources are cut at 65,534 bytes** without warning (the original read into a fixed
64 KB buffer; an include that pushed the text past it overran the buffer — see
[F4](#f4)).

### 10.4 Original bugs — expressions

None of these is triggered by any referenced shipped script (instrumented scan, Appendix
B), so all are *safe to fix*.

#### E1
**`<=` and `>=` are dropped.** `IsBinaryOperator` (0x00412540) accepts operators 4–12 and
15 but not 13/14, so the operator is never stored. The original's evaluator (0x00411650)
then reads the operator list past its end — an access violation when the node had no other
operator. The port treats the missing operator as "no operation", so `x <= 3` evaluates to
`x`.

#### E2
**`!=` / `<>` bind looser than `AND` / `OR`.** `!=` has the comparison precedence class,
but the grouping pass for comparisons only collects `>`, `<`, `==` (0x00412760). In a
mixed expression it is therefore grouped last: `a == 1 AND b != 2` is evaluated as
`((a == 1) AND b) != 2`, which is **always true** (reproduced). Parenthesise.

#### E3
**`NOT` before a parenthesis is ignored**: `NOT (a == 5)` equals `(a == 5)`.

#### E4
**A `-` before a variable is a compile error.** `AddToken` refuses a variable directly
after `-` (it only folds signs into literals), so `a - b` ends the expression at `b` and
fails. Workaround: `a - (b)`.

#### E5
**Sign handling.** A `-` is folded into the following literal only after `(`, `*`, `/`,
`==`, `!=`, `<=`, `>=`, `<` or at the start — and **the `-` token stays in the expression as
a subtraction**. Results: `-5 + 2` = -7, `a + -1` = a + 1, `a - -1` = a - 1,
`x == -1` → x and `-5 * 2` → -5 (the port crashed on these before [P2](#p2) was fixed).
Parentheses around the literal work.

#### E6
**Use after free when grouping.** The original's `GroupRange` (0x00412560) deletes the
first node it moved into the new group, leaving a dangling child that a later allocation
may reuse; script unload then frees it twice. *Fixed in the port.* Only one (unreferenced)
shipped expression even reaches this code.

#### E7
**Division by zero** crashed the original (integer divide fault). *Fixed in the port*
(result 0).

#### E8
**String expressions** only concatenate with `+`; other operators drop their right side,
function calls contribute nothing, and string comparisons are meaningless
([5.6](#56-string-expressions)).

#### E9
**Function arguments must be single operands** of the declared type — no expressions.

#### E10
**`true` / `false` are not accepted inside expressions** (only as command arguments).

### 10.5 Original bugs — interpreter

#### R1
**`Delay(0)` stalls the script forever.** `Tick` (0x004167b0) sets the delay to 0 and does
not advance; a zero delay is never counted down. The same happens to activities whose
duration is 0 — which is what `PlayActivity` returns for a human that is not under its
scripted controller. *Safe to fix* for `Delay(0)` (not used by shipped scripts).

#### R2
**`GoTo`, `Gosub`, `Return` and `Delay` are ignored inside `Block`.** `ExecuteBlock`
(0x0041dc00) calls the command handler directly, and those four are only handled by
`Tick`. *Changes shipped behaviour*: `GoTo` inside a block occurs in 12 referenced scripts
and `Delay` in 3 ([S6](#s6), [S7](#s7)).

#### R3
**A `Whenever` declared inside `Block` also skips the command after its body**: the
handler (0x00416e10 case 0x0D) jumps past the body and then calls `Advance()`, which steps
once more. Not in shipped scripts.

#### R4
**Waiting commands inside `Block` are not waited for**, and some leave state behind:
`HUMAN_Suspend(true)` re-runs the command before it and leaves a 2 s delay that, once
expired, skips the first command after the block; `FRM_MorphSpeechDelayed` likewise skips
one command after the block. In shipped content `HUMAN_Suspend(true)` inside a block only
occurs in the non-compiling CASTLE1 scripts (the 347 other uses are `HUMAN_Suspend(false)`).

#### R5
**More than 100 nested `Gosub`s stop the script.** The 101st `Gosub` neither jumps nor
advances (0x004167b0 case 0x51). A `Gosub` whose subroutine ends with `GoTo` instead of
`Return` leaks one stack entry per call. Shipped case: [S8](#s8).

#### R6
**`Return` with an empty call stack ends the script**, and handlers do not clear the call
stack, so a `Return` inside a handler returns into the interrupted subroutine's caller.

#### R7
**Unhandled or failing commands stop a generic script for good.** The command handler
returns "not handled" without advancing, and generic scripts have no fallback; the script
then waits until an event moves it. Cases: any `HUMAN_*` / `FRAME_SETAIMODE_*` / `DOG_*`
command on a frame that is not a human / dog, `FRM_Move`, `FRM_GetPosition`,
`SendSignal(null, n)`, `FRM_StopAnimation(null, n)`. Some commands stall every script
kind: `FRM_SetLightMap` on a non-visual frame, `HUMAN_Drive` / `HUMAN_DriveNearPlayer` when
the human is not under its scripted controller, `HUMAN_Bike` ([K7](#k7)).

#### R8
**Some commands skip the next command in human and dog scripts.** `__Log`, `__LogNumber`,
`__LogFrame`, `FRM_TimerSetOn`, `THUNDERSTORM_MP` and `HUMAN_KillAllInBox` with a
non-dummy frame advance *and* report "not handled"; `C_HumanScript` / `C_MeleeTrackerScript` then
advance a second time (0x00421c00 default case). Not triggered by shipped scripts.

#### R9
**Weak handler protection and inconsistent wake-ups.** `SendSignal`, `OnUse` and `OnHit`
refuse to interrupt `OnDeath` / `OnAlarm` only in the tick those start (the check is
"is the instruction pointer exactly at the handler's entry"). `OnUse`, `OnHit` and
`OnGlobalAlarm` do not cancel a running `Delay`, so the handler starts only after it; and
they, `OnAlarm` and signals cannot wake a finished script while `OnDeath`, `OnAlarmDone`
and the cutscene handlers can (original 0x0041d920–0x0041dbb0).

#### R10
**Whenevers can hijack anything, and fire twice in a long tick.** A firing Whenever does
not respect `OnDeath`; when one tick covers several 500 ms steps, a second Whenever can
fire after the first, overriding its jump although the first is already disabled.

#### R11
**Signals to humans that are not under their scripted controller are lost** when a handler
exists — neither run nor remembered for `_SignalReceived`.

#### R12
**`ScriptAssign` on the running script's own frame frees the running script** (its `OnExit`
runs, it is deleted) and the interpreter then keeps using it (`Advance`, tick timing): use
after free. It also edits the script list while the module iterates over it, so another
script may be ticked twice or skipped that tick. Re-assigning a frame that has a script
needs two calls (the first only removes). Original 0x00416e10 case 0 and 0x00427730.

#### R13
**String variables are not saved** in saved games (0x004147d0 writes only numbers and
frames); after loading they are empty. No shipped script uses String variables.

#### R14
**`FRM_SetAnimationDelayed` always waits on animation stage 0**, whatever stage it started;
with nothing on stage 0 the wait dereferences null. Shipped uses all use stage 0.

#### R15
**A cutscene that nobody handles never ends**, because only scripts in an `OnCutscene`
handler run during it. Starting a cutscene while one runs overwrites the saved camera
(one reference leaks, and the "saved" aspect / FOV are the cutscene's).

#### R16
**Cutscene view assumptions.** `EndCutscene` always sets the camera's aspect to **4:3**
([script_module.cpp:714](../src/script/script_module.cpp#L714)) and the letterbox band is
`width × 9 / 16` high, which is taller than the screen on displays wider than 16:9
([script_module.cpp:538](../src/script/script_module.cpp#L538)). Relevant for the port on
modern screens.

#### R17
**`FRM_SetActiveCamera` on a frame that is not a camera** hides the HUD and panel (the
check for "game camera" comes before the type check) without switching the camera.

### 10.6 Original bugs — commands and functions

#### K1
**Missing null checks.** `_ItemInRange`, `_FrameInRange`, `_CameraInRange`,
`_GetDistToActivePlayer` and `_MP_AllTeamMembersAreInRange` / `_MP_IsTeamMemberInRange`
use the script frame unchecked (crash in a script without a frame); `_PlayerInBox(null)`
and `CSC_SetCameraFOV` without an active camera crash too (original 0x0041e3f0).

#### K2
**`_GetDistToActivePlayer` returns INT_MIN without an active player**, so
`_GetDistToActivePlayer() < 10` is true.

#### K3
**`_PlayerInRange` returns a count**, not a boolean (fine in conditions; matters in
arithmetic).

#### K4
**`_MP_IsTeamMemberInRange` and `_MP_AllTeamMembersAreInRange` fail on any dead player.**
Both walk the player slots from 31 down and return 0 at the first dead but connected
player — of either team — before checking the team (original 0x0041e3f0 cases 0x17,
0x18). `_MP_IsTeamMemberInRange` is therefore order dependent.

#### K5
**`SetNPCTeamStatus(x, true)` ends the calling script even if `x` is not an NPC team
member**; `SetNPCTeamStatus(x, false)` does nothing at all.

#### K6
**`HUMAN_ACTIVITY_Smoke` is not implemented** (no case in 0x00421c00), yet shipped scripts
call it 252 times in 92 files expecting soldiers to smoke; it does nothing. 19 other
commands are not implemented either ([8.14](#814-commands-that-do-nothing)).

#### K7
**`HUMAN_Bike`** never continues when given a destination (no completion label is passed
to the bike), and stalls when the bike or the road point is not found. Unused by shipped
scripts.

#### K8
**Weather networking.** `WEATHER_SetMode` sends only "mode == 1" to the clients while the
server applies the raw value; `THUNDERSTORM_SetOn` is applied locally only and never sent.

#### K9
**8-bit network fields**: particle ids (`FRM_CreateParticle`), light map levels and the
near / far distances of speech are truncated to 0–255.

#### K10
**`HUMAN_KillAllInBox` kills players too** — every playing human in the box gets the
explosion hit.

### 10.7 Original bugs the port fixes

| ID | Original bug | Port |
|---|---|---|
| <a id="f1"></a>F1 | Division by zero in expressions crashed. | Result 0. |
| <a id="f2"></a>F2 | `<=` / `>=` read the operator list out of bounds ([E1](#e1)). | Treated as no operation. |
| <a id="f3"></a>F3 | Use after free / double free in operator grouping ([E6](#e6)). | Nodes kept alive. |
| <a id="f4"></a>F4 | Included text could overrun the static 64 KB source buffer (e.g. a file that includes itself), per the port's notes. | `std::string`, at most 64 includes. |
| <a id="f5"></a>F5 | The operator look-ahead counted line breaks twice (per the port's notes). | Line counter rewound. |

(The `sprintf` the original used to copy string literals sized its buffer exactly, so it
did not overflow for ordinary text. Removing it caused [P1](#p1); the port now reproduces
its formatting without calling `sprintf`.)

### 10.8 Errors in the shipped scripts

These are mistakes in the game's own scripts, found with the port's compiler and the
instrumented scan. They behave the same way in the original game.

| ID | Script(s) | Problem | Effect |
|---|---|---|---|
| <a id="s1"></a>S1 | `AFRICA3\AF3a_31.scr` (referenced) | Stray `7` before the first comment | Never compiles: that soldier runs without a script. |
| <a id="s2"></a>S2 | `Alps3_obj\Alps3_obj_car_01..03.scr` (unreferenced) | `OnDeath{` without `()` | Never compile. |
| <a id="s3"></a>S3 | 38 scripts in `CASTLE1\` (unreferenced; no `Missions\CASTLE1`) | `HUMAN_SETSIGHTRANGE(a, b)` with two arguments | Never compile. |
| <a id="s4"></a>S4 | 8 `scripts.dta` / `mpscripts.dta` entries (e.g. `AFRICA2` → `AF2_particle_junkers.scr`, `BURMA2` → `CUTSOUNDSHLIDKA.scr`) | Name a script file that does not exist | Those frames get no script. |
| <a id="s5"></a>S5 | `CZECH1\speech5.scr` (referenced) | `Whenever player(...)` without a body, followed by `OnSignal(1)` | The Whenever "skips" into the middle of the next handler, so the speech starts at mission start regardless of the player's distance, and restarts when the player comes within 20 m ([C3](#c3)). |
| <a id="s6"></a>S6 | `ALPS1\ge_26..28`, `CZECH2\R_Cz2_Ger17..19`, `R_Cz2_Ger22..23`, `ger30`, `ger31`, `CZECH3\solcar_2`, `NORWAY\bonzak` (referenced) | `goto` inside `block{}` | Ignored ([R2](#r2)); e.g. in `ge_26` the soldier falls through into the following `pomoc` route instead of ending. |
| <a id="s7"></a>S7 | `AFRICA3\AF3a_02`, `AF3a_03`, `Burgundy1\bu1_o03_driver` (referenced) | `Delay` inside `block{}` | Ignored; e.g. `AF3a_03` ends at once instead of after 2 s. |
| <a id="s8"></a>S8 | `AFRICA3\AF3a_16.scr` (referenced) | `gosub SMOKE` whose subroutine ends with `goto activity` instead of `return` | The second half of the idle loop never runs, and after ~100 loops (about two hours) the next `gosub` freezes the soldier ([R5](#r5)). |
| <a id="s9"></a>S9 | 92 scripts | `HUMAN_ACTIVITY_Smoke` | Does nothing ([K6](#k6)). |
| <a id="s10"></a>S10 | `BURMA2\CUTHLIDKA2.scr` | `"%B_engvtabore.I3D"` with a single `%` | The original's `sprintf` drops the `%` (MSVC prints the character after an unknown conversion), so the file `%B_engvtabore` is most likely not found. The port does the same ([P1](#p1)). |
| <a id="s11"></a>S11 | `TUTORIAL\T_EE_Weather.scr` (unreferenced) | Comparisons with negative literals | Mis-evaluated (`X < -60` is just `X`); the port crashed on it until [P2](#p2) was fixed and now evaluates it the same way. |

---

## Appendix A — Opcodes outside the command table

| Opcode | Meaning |
|---|---|
| 70–75 | Declaration keywords `Integer`, `Real`, `Bool`, `String`, `Enum`, `Frame` (no command emitted) |
| 76 | `Label` (no command) |
| 77, 81, 82 | `GoTo`, `Gosub`, `Return` |
| 78, 79 | `If`, `Else` |
| 80 | `Whenever` |
| 83–92, 164 | Handlers (`OnSignal` 83, `ScriptStartPoint` 84 — no command, `OnDeath` 85, `OnUse` 86, `OnAlarm` 87, `OnHit` 88, `OnGlobalAlarm` 89, `OnExit` 90, `OnAlarmDone` 91, `OnCutsceneDone` 92, `OnCutscene` 164) |
| 261, 262 | `{`, `}` |
| 263 | Assignment (`=` keyword token); also marks the first operator token |
| 264–273 | Operator tokens `+ - * / > < == >= <= !=/<>` (only inside expressions) |
| 274 | `Block` (outside the interpreter's 1..263 range; handled by `Tick` only) |
| 30, 49 | Unused (no keyword, no table entry) |
| 34 | Table entry with one Frame argument but no keyword: unreachable |
| 119, 121 | `FRM_Move`, `FRM_GetPosition`: keywords without table entry or implementation |

Keyword tokens are looked up from the end of the keyword table (0x00835b68), so operators
win over same-named entries; `FRM_CreateParticle` is listed twice. The table also carries
editor syntax colours (white for most commands, magenta for the language keywords and core
commands, yellow for punctuation and operators), a trace of the in-game script editor; the
command table carries argument hints such as
`"game speed [zotti ;-) ], (1000 = normal speed)"`.

---

## Appendix B — How the findings were verified

1. **Code reading**: the port's `src/script` files, compared with the original's
   decompilation for every function quoted above (addresses in the text). The
   interpreter's main switch (0x00416e10) is too large for the decompiler; its cases were
   read from `_re/vm.txt` and the case map `_re/vm_cases.json`, and the Whenever parser from
   the live disassembly.
2. **Test harness** (scratch, not part of the project): the port's real
   `script_compiler.cpp`, `script_expression.cpp`, `script_tables*.cpp` and `c_str.cpp`,
   compiled with MinGW g++ against stub `game/game.h` / `ls3d/rw_data.h` and a minimal
   `C_Script`. It compiles a script, runs its assignments, dumps the command list and
   prints variables. Every expression example in [5](#5-expressions) and every *port* bug
   was reproduced with it (the [P2](#p2) crash with a gdb backtrace). After the P1–P4
   fixes, the same harness confirmed each fix with targeted test scripts.
3. **Shipped corpus**: all 5,347 `.scr` files compiled with that harness. Before the
   fixes: 5,303 compile, 43 fail as listed in [S1](#s1)–[S3](#s3) plus one include
   fragment, 1 crashes ([S11](#s11)). After them: 5,304 compile, the same 43 fail, none
   crashes. An instrumented copy of the expression code reported every trigger of
   [E1](#e1)–[E6](#e6), and a scan of the compiled command lists found the `Block`,
   `Whenever` and `Gosub` cases. The 79 `scripts.dta` / `mpscripts.dta` files were parsed
   to tell referenced scripts (4,794) from unreferenced ones (553). Usage counts in
   [8](#8-commands) come from the same scan.

Corpus statistics:

| | |
|---|---|
| Script files / mission folders | 5,347 / 71 |
| Assignments in `scripts.dta` + `mpscripts.dta` | 5,991 (8 name missing files) |
| Referenced scripts / that compile | 4,794 / 4,793 |
| Most used commands | `GoTo` 13,797 · `FRM_FindFrame` 13,673 · `SetAlarmType` 11,526 · `If` 9,574 · `Delay` 8,526 · `Block` 8,341 · `HUMAN_Move` 6,516 |
| Never used | `Real`, `Bool`, `Enum`, `String`, `ScriptStartPoint`, `OnGlobalAlarm`, `<=`, `>=`, and 44 commands (e.g. `HUMAN_Bike`, `HUMAN_LookAt`, `FRM_SoundAdjustVolume`, `AddDebriefingText`) |
