# Oryn-ML

Oryn-ML is a small functional language inspired by the ML family. Its compiler
turns `.oryn` source files into readable C and can invoke a standard C compiler
to produce native executables.

The compiler is written in C11 with no third-party dependencies. The project
also includes a compact runtime for tagged values, collections, closures, and
automatic memory management.

```text
program.oryn
     │
     ▼
 lexer → parser → AST → semantic analysis → C code generation
                                                    │
                                                    ▼
                                        generated C + runtime.c
                                                    │
                                                    ▼
                                          native executable
```

> Current status: executable `v0.1` foundation. The language is ready for
> experimentation, examples, and compiler development, but is not intended to
> replace a production language yet.

## Contents

- [Quick start](#quick-start)
- [Command-line interface](#command-line-interface)
- [Complete example](#complete-example)
- [Language guide](#language-guide)
- [Built-in API](#built-in-api)
- [How compilation works](#how-compilation-works)
- [Diagnostics](#diagnostics)
- [Testing and development](#testing-and-development)
- [VS Code extension](#vs-code-extension)
- [Current limitations](#current-limitations)
- [Project structure](#project-structure)

## Quick start

### Requirements

- a POSIX environment such as Linux or macOS;
- `make`;
- a C compiler with C11 and GNU statement-expression support, such as Clang or
  GCC.

### Build the compiler

```sh
make
```

The compiler is created at `bin/oryn`.

### Run your first program

Create `hello.oryn`:

```oryn
let language = "Oryn-ML";
println("Hello from " + language);
println(40 + 2);
```

Compile and run it directly:

```sh
bin/oryn run hello.oryn
```

Output:

```text
Hello from Oryn-ML
42
```

You can also generate and inspect the intermediate C source:

```sh
bin/oryn hello.oryn -o bin/hello.c
cc -std=gnu11 -Iruntime/include \
  bin/hello.c runtime/src/runtime.c -o bin/hello
bin/hello
```

## Command-line interface

```text
oryn <input.oryn> [-o output.c]    generate C source
oryn check <input.oryn>            parse and analyze without generating output
oryn build <input.oryn> [-o exe]   create a native executable
oryn run <input.oryn>              compile and run
oryn --help                        show command help
oryn --version                     show the compiler version
```

Examples:

```sh
# Check syntax and semantics
bin/oryn check examples/complete.oryn

# Generate readable C
bin/oryn examples/complete.oryn -o bin/complete.c

# Create a persistent native executable
bin/oryn build examples/complete.oryn -o bin/complete

# Compile and run with temporary files
bin/oryn run examples/complete.oryn
```

The `build` and `run` commands invoke `cc`. The `run` command removes its
temporary C source and executable after the program exits.

## Complete example

The following program parses text safely, transforms a collection, and
calculates a summary:

```oryn
fn clamp(score: Int) -> Int {
    if score < 0 { 0 } else {
        if score > 100 { 100 } else { score }
    }
}

fn add_bonus(score: Int) -> Int {
    clamp(score + 5)
}

let input = "82, 91, invalid, 64, 105, 73";

let scores = input
    .split(",")
    .map(fn text: String => Int.parse(text.trim()).unwrap_or(0))
    .map(fn score: Int => clamp(score));

let minimum = 60;
let passing = scores.filter(fn score: Int => score >= minimum);
let adjusted = passing.map(fn score: Int => score |> add_bonus);
let total = adjusted.fold(0, fn(acc: Int, score: Int) => acc + score);
let average = if adjusted.is_empty() { 0 } else {
    total / adjusted.len()
};

println("Scores: ");
println(adjusted);
println("Average: ");
println(average);

match adjusted.first() {
    Some(score) => println("First score: ", score),
    None => println("No valid scores")
};

assert(average == 86);
```

An executable version covered by the integration suite is available at
[`examples/complete.oryn`](examples/complete.oryn).

## Language guide

### Values and types

Oryn-ML provides the primitive types `Int`, `Float`, `Bool`, and `String`, as
well as arrays, functions, unit, `Option<T>`, and `Result<T, E>`.

```oryn
let count: Int = 10;
let ratio: Float = 2.5;
let enabled: Bool = true;
let name: String = "Oryn";
let values: Array<Int> = [10, 20, 30];
```

Bindings declared with `let` are immutable. Arrays can contain heterogeneous
runtime values, while generic annotations enable more precise semantic checks.

### Operators

```oryn
let arithmetic = (10 + 2) * 3 / 2;
let remainder = 10 % 3;
let comparison = arithmetic >= 18 && arithmetic != 0;
let negated = !false;
```

Available operators:

- arithmetic: `+`, `-`, `*`, `/`, and `%`;
- comparison: `==`, `!=`, `<`, `<=`, `>`, and `>=`;
- boolean: `&&`, `||`, and `!`;
- numeric unary negation: `-`;
- pipeline: `|>`.

Strings can be concatenated with `+`, and arrays support structural equality.

### Conditionals are expressions

Every `if` branch produces a value:

```oryn
fn classify(value: Int) -> String {
    if value < 0 { "negative" } else {
        if value == 0 { "zero" } else { "positive" }
    }
}

let label = if true { "active" } else { "inactive" };
```

### Functions and recursion

```oryn
fn factorial(n: Int) -> Int {
    if n <= 1 { 1 } else { n * factorial(n - 1) }
}

println(factorial(6));
```

Named functions accept annotated parameters, may declare a return type, and
can call themselves recursively. The final expression in the body becomes the
return value.

### Pipelines

The `|>` operator passes its left-hand value as the first argument to the
function on its right:

```oryn
fn double(value: Int) -> Int {
    value * 2
}

println(21 |> double);
```

### Anonymous functions and closures

Anonymous functions can capture bindings from their lexical scope:

```oryn
let offset = 10;
let shifted = [1, 2, 3].map(fn value: Int => value + offset);

println(shifted); // [11, 12, 13]
```

Use parentheses for two or more parameters:

```oryn
let total = [1, 2, 3].fold(
    0,
    fn(acc: Int, value: Int) => acc + value
);
```

In the current version, anonymous-function parameters should be annotated when
their bodies use type-specific operators.

### Arrays and functional transformations

Arrays are immutable. Transformations return new values and run eagerly:

```oryn
let numbers = [1, 2, 3, 4, 5];
let result = numbers
    .filter(fn n: Int => n % 2 != 0)
    .map(fn n: Int => n * n)
    .fold(0, fn(acc: Int, n: Int) => acc + n);

println(result); // 35
```

Index access is safe:

```oryn
let numbers = [10, 20, 30];

match numbers.get(2) {
    Some(value) => println(value),
    None => println("Index not found")
};
```

Arrays also support concatenation, membership checks, reversal, and checked
slicing:

```oryn
let values = [1, 2, 3].concat([4, 5]);

assert(values.contains(4));
println(values.reverse());

match values.slice(1, 4) {
    Ok(part) => println(part),
    Err(message) => panic(message)
};
```

### `Option` and missing values

`Option<T>` represents a present or missing value without using `null`:

```oryn
let values = [10, 20, 30];

match values.last() {
    Some(value) => println(value),
    None => println("Empty array")
};

let fallback = values.get(99).unwrap_or(-1);
```

A `match` over `Option` must cover both `Some` and `None`, or include the `_`
wildcard arm.

### `Result` and recoverable failures

Parsing and slicing return `Result` instead of terminating the program:

```oryn
match Int.parse("8080") {
    Ok(port) => println(port),
    Err(message) => println(message)
};

match [10, 20, 30, 40].slice(1, 3) {
    Ok(part) => println(part),
    Err(message) => panic(message)
};
```

The `unwrap_or`, `is_ok`, and `is_err` helpers are also available:

```oryn
let port = Int.parse("invalid").unwrap_or(3000);
assert(Float.parse("2.5").is_ok());
assert(Float.parse("oops").is_err());
```

### Strings

```oryn
let normalized = "  Oryn ML  ".trim().to_lower();

assert(normalized.contains("oryn"));
assert(normalized.starts_with("oryn"));
assert(normalized.ends_with("ml"));

println("a,b,c".split(","));
println(normalized.to_upper());
```

In `v0.1`, string length and case conversion are byte/ASCII-oriented rather
than Unicode code-point or grapheme-aware.

### Comments

Line comments start with `//`:

```oryn
// The compiler ignores this line.
let answer = 42;
```

## Built-in API

### Global functions and constructors

| Function | Description |
| --- | --- |
| `print(values...)` | Print values without appending a newline. |
| `println(values...)` | Print values and append a newline. |
| `assert(condition)` | Terminate if the condition is false. |
| `panic(message)` | Terminate immediately with a message. |
| `len(value)` | Return the length of a string or array. |
| `get(array, index)` | Return an `Option<T>` for an array index. |
| `parse_int(text)` | Convert text to `Result<Int, String>`. |
| `parse_float(text)` | Convert text to `Result<Float, String>`. |
| `to_string(value)` | Return the textual representation of a value. |
| `Some(value)` / `None` | Construct optional values. |
| `Ok(value)` / `Err(error)` | Construct result values. |

`Int.parse(text)` and `Float.parse(text)` provide type-oriented numeric parsing.

### Methods

| Receiver | Methods |
| --- | --- |
| `String` | `len`, `is_empty`, `contains`, `starts_with`, `ends_with`, `trim`, `to_lower`, `to_upper`, `split` |
| `Array<T>` | `len`, `is_empty`, `get`, `first`, `last`, `contains`, `concat`, `reverse`, `slice`, `map`, `filter`, `fold` |
| `Option<T>` | `unwrap_or` |
| `Result<T, E>` | `unwrap_or`, `is_ok`, `is_err` |

## How compilation works

1. `driver.c` reads the file and selects the CLI workflow.
2. `lexer.c` turns source text into position-aware tokens.
3. `parser.c` builds the AST defined in `ast.h`.
4. `sema.c` checks declarations, scopes, arity, annotations, operators,
   methods, and `match` coverage.
5. `codegen_c.c` walks the validated AST and emits GNU11-compatible C.
6. Generated code calls the ABI declared in `runtime/include/oryn/runtime.h`.
7. For `build` and `run`, the driver compiles the result together with
   `runtime/src/runtime.c`.

The runtime represents values with the tagged union `OValue`. Strings, arrays,
closures, and algebraic payloads are tracked by a mark-and-sweep collector that
runs at safe points in generated code.

## Diagnostics

Semantic errors include the file, position, source excerpt, and a caret:

```text
oryn: example.oryn:2:5: semantic error: cannot redefine 'value'
let value = 20;
    ^
```

Implemented checks include:

- undefined names and duplicate declarations;
- argument count and compatibility;
- operators applied to incompatible types;
- method receivers and arguments;
- recursive generic annotations for `Array`, `Option`, and `Result`;
- `Option` and `Result` coverage in `match` expressions;
- wildcard-arm ordering.

## Testing and development

### Full test suite

```sh
make test
```

Each source in `examples/*.oryn` is compiled to C, built as a native program,
and compared with its golden output in `tests/expected/`. The suite also checks
that programs under `tests/invalid/` are rejected.

### Sanitizers

```sh
make sanitize
```

This compiles and runs every example with AddressSanitizer and
UndefinedBehaviorSanitizer.

### Formatting and static checks

```sh
make format         # format C sources and headers
make format-check   # check formatting without changing files
make lint           # formatting plus strict C compiler diagnostics
make examples       # build every example as a native executable
make clean          # remove generated artifacts from bin/
```

Use the `CC` variable to select another compiler:

```sh
make CC=clang test
make CC=gcc test
```

## VS Code extension

The extension under `editors/vscode/` provides:

- `.oryn` file association;
- syntax highlighting;
- automatic bracket closing;
- indentation and comment toggling;
- completion for built-in functions and methods;
- hover documentation for native APIs.

To test it during development, open `editors/vscode/` in VS Code and press
`F5`.

To package and install it locally:

```sh
npx --yes @vscode/vsce package editors/vscode \
  --out bin/oryn-ml-0.1.2.vsix
code --install-extension bin/oryn-ml-0.1.2.vsix --force
```

## Current limitations

Version `v0.1` does not yet provide:

- modules and imports;
- user-declared algebraic data types;
- file and path APIs;
- fully Unicode-aware text operations;
- complete static inference for every value;
- arbitrary expression calls—ordinary calls currently target named functions;
- a backend independent of C.

Some checks happen statically. Values whose types cannot be inferred retain
runtime kind checks.

Generated `match` expressions use the statement-expression extension supported
by GCC and Clang. Compile emitted code with `-std=gnu11`, not only `-std=c11`.

## Project structure

```text
.
├── compiler/
│   ├── include/oryn/compiler/  APIs shared by compiler phases
│   └── src/                    lexer, parser, sema, codegen, and driver
├── runtime/
│   ├── include/oryn/runtime.h  ABI used by generated C
│   └── src/runtime.c           values, built-ins, collections, and GC
├── examples/                   executable Oryn-ML programs
├── tests/
│   ├── expected/               golden output for examples
│   ├── invalid/                programs that must be rejected
│   ├── run.sh                  integration test runner
│   └── sanitize.sh             sanitizer test runner
├── editors/vscode/             VS Code language support
├── LICENSE                     MIT license terms
└── Makefile
```

The smaller examples are organized by feature:

| File | Demonstrates |
| --- | --- |
| `hello.oryn` | primitive values and output |
| `conditionals.oryn` | expression-oriented `if` and boolean operators |
| `functions.oryn` | named functions and recursion |
| `functional.oryn` | closures, `map`, `filter`, and `fold` |
| `collections.oryn` | safe access and array queries |
| `arrays.oryn` | concatenation, reversal, slicing, and equality |
| `text.oryn` | string queries |
| `parsing.oryn` | numeric parsing and fallbacks |
| `options.oryn` | `Option`, `Some`, and `None` |
| `result.oryn` | `Result`, `match`, and pipelines |
| `complete.oryn` | an integrated example covering most of `v0.1` |

## License

Oryn-ML is available under the [MIT License](LICENSE). You may use, copy,
modify, merge, publish, distribute, sublicense, and sell copies of the
software, provided that the copyright and license notices are retained.
