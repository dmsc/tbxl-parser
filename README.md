TurboBasic XL parser tool
-------------------------

This program parses and tokenizes a _TurboBasic XL_ listing in a flexible format
and produces any of three outputs:

- A tokenized binary file, directly loadable in the original _TurboBasic XL_
  interpreter.  This mode also replaces variables with single letters by
  default, but with the `-f` option writes the full variable names and
  with the `-x` option writes invalid variable names, making the program
  unable to be listed or edited.

  This is the default operating mode, and also can be forced with the `-b`
  command line switch.

- A minimized listing, replacing variable names with single letters, using
  abbreviations, removing spaces and using Atari end of lines.

  This mode is selected with the `-s` command line switch.

- A pretty printed expanded listing, with one statement per line and
  indentation, and standard ASCII line endings.

  Note that this format can be read back again, but some statements are
  transformed in the process, this can lead to problems in non-standard
  IF/THEN constructs.

  Currently, `IF`/`THEN` with statements after the `THEN` are converted to
  multi-line `IF`/`ENDIF` statements.

  This mode is selected with the `-l` command line switch.


The input listing format is very flexible:

- Standard Atari listing format, with Atari or ASCII end of lines.  The parser
  understand the same abbreviations available in the original interpreter.

- A free format listing, with line numbers separated from the statements.

  In this format, not each line needs line number, only lines that are target
  to `GOTO` / `GOSUB` / `THEN` needs them.  If you use only labels, no line
  numbers are needed.

  Also, line numbers can appear alone in a line, for better readability.

- Adds parameters and local variables to PROC/EXEC.

  Arguments follow the PROC label after a comma, and local variables follow
  after a semicolon:
  ```
    D = 3
    EXEC Testing, D+5, "Hello"
    PRINT D
    PROC Testing, A, B$(10); D
        D = A + 1
        PRINT D; " and "; B$
    ENDPROC
  ```

  As the example shows, string variables must include the dimensioned length,
  as the parser adds a DIM at the start of the program to initialize. The
  dimensioned length must be an integer, a `$define` or a `%` number.

  Also, setting the value of "D" inside the procedure does not alter the value
  of the variable "D" outside the procedure.

- Inside strings, special characters can be specified by using a backslash
  followed by an hexadecimal number in upper-case, (i.e., `"\00\A0"` produces
  a string with a "hearth" and an inverse space), this allows editing special
  characters on any ASCII editor. Note that to force a backslash before a
  valid hex number, you can use two backslashes (i.e., ``"123\\456"`` produces
  ``123\456``).

- Comments can be started by `'` in addition to the _TurboBasic_ `.`, `--` or
  `rem`.

- The input is case insensitive (uppercase, lowercase and mixed case is
  supported).

- There are parsing *directives* added, that consist on lines starting with a
  dollar sign `$`. A list of available directives documented bellow.

- There is support for extended strings, with embedded character names.
  Start the string with `["` and end the string with `"]`, and include
  special characters with `{name}` or `{count*name}`, with count a decimal
  number and name from the list:
  `heart`, `rbranch`, `rline`, `tlcorner`, `lbranch`, `blcorner`, `udiag`,
  `ddiag`, `rtriangle`, `brblock`, `ltriangle`, `trblock`, `tlblock`,
  `tline`, `bline`, `blblock`, `clubs`, `brcorner`, `hline`, `cross`, `ball`,
  `bbar`, `lline`, `bbranch`, `tbranch`, `lbar`, `trcorner`, `esc`, `up`,
  `down`, `left`, `right`, `diamond`, `spade`, `vline`, `clr`,
  `del`, `ins`, `tbar`, `rbar`, `eol`, `bell`.

Example Programs
----------------

There are two sample programs, located in the `samples` folder that illustrate
the free-form input format.

```
  ' Example program

  ' One statement per line:
  print "Hello All"
  print "---------"
  print "This is a heart: \00"

  ' Also, multiple statements per line:
  for counter = 0 to 10 : ? "Iter: "; counter : next counter

  ' Line numbers
30
  ' And abbreviations:
  g. 30

```

To generate a BAS file, loadable by _TurboBasic_, simply type:

    basicParser samples/sample-1.txt

This will generate a `sample-1.bas` file in the same folder.


Program Usage
-------------

    basicParser [options] [-o output] filenames

Options:

- `-n nun`  Sets the maximum line length before splitting lines to `num`.
            Note that if a single statement is longer than this, the line
            is output anyway.
            The default is 120 characters (the standard Atari Editor limit)

- `-l`  Output long (readable) listing, suitable for editing, with ASCII
  end of lines and lowercase statements.

- `-s`  Output a short, minimized listing, with ATASCII end of lines. The
  default output file name is the same as input with `.lst` extension added.

- `-b`  Output a binary tokenized file instead of a listing. The  default
  output file name is the same as input with `.bas` extension added. Note that
  this is the default behaviour.

- `-x`  In binary output mode, writes null variable names, making the program
  unlistable.

- `-f`  In binary output mode, writes the full variable names, this eases
  debugging the program.

- `-a`  In long output, replace Atari characters in comments with
  approximating characters.

- `-v`  Shows more parsing information, like name of renamed variables.
  (verbose mode)

- `-q`  Don't show any parsing output, only errors.  (quiet mode)

- `-o`  Sets the output file name.  By default, the output is the name of the
  input with `.lst` (listing) or `.bas` (tokenized) extension.

- `-c`  Output to standard output instead of a file.

- `-h`  Shows help and exit.

Parser directives
-----------------

Directives add extra features to the parser, much like C and C++. Directives
start with a dollar as the first non blank character on a line, and continue
up to the end of the line.

Bellow is a description of available directives.

### `$options` directive.

The options directive alter the way the parsing is done, accepting a list of
comma separated options, valid for the current file. Valid options:
- `mode=compatible`: Disable features to be more compatible with the
  _TurboBasic XL_ parser.
- `mode=extended`: Makes the parser to accept more extended features.
- `mode=default`: Returns the parser to the default mode.
- `optimize` or `+optimize`: Allows the parser to optimize the output to
  produce smaller or faster code.
- `-optimize`: Disable the optimizations.
- `optimize=+`*suboption*: Enable the particular optimization option.
- `optimize=-`*suboption*: Disable the particular optimization option.

The optimization sub-options are:
- `const_folding`: Replace operations on constants with the result.
- `convert_percent`: Replace small integers with the `%*` equivalent.
- `commute`: Swap arguments to binary operations to minimize runtime.
- `line_numbers`: Remove all Basic line numbers that are unused.
- `const_replace`: Replace repeated constant values (numeric or string) with
  a variable initialized to the value. The initialization code is added
  before any statement in the program, and tries to use the minimum number
  of bytes posible.

Note that options can be changed at any place in the file, this is an example
of changing the parser mode in the middle of the file:

```
  ' Example program using directives
  $ options optimize, mode=default
  error1 = 2
  ? error1   : ' This is parsed like TurboBasic XL, as ? ERR OR 1
  
  $options mode = extended
  ? error1   : ' This is parsed as ? error1
  Printa     : ' This is a parsing error.
```

A good optimization mode for producing short listings is
`$options +optimize, optimize=-convert_percent-const_replace` , this avoid
converting numbers to `%` values and the replacement of constants, producing
a smaller listing. Note that replacement of constants can be beneficial, so
try enabling the optimization and running with "-v" option to see what
variables are good candidates for replacement.

### `$define` directive.

This directive defines new symbols that are replaced at parsing time with the
values, like C macros.

Replacement names are prefixed by `@` to differentiate from variables, and
as variables, string defines end in `$`, the syntax of the directive is

  $define *defineName* = *value*

Keep in mind that as the value is replaced each time the variable is used, it
is probably best to assign them to a variable instead if the value will be
used multiple times, and you should enable optimizations so that the usage is
simplified at parsing time.

This is an example usage of the `$define` directive:

```
  ' Example usage of defines
  $options +optimize
  $define Message$ = "Hello world!"
  $define PCOLR0   = $2C0
  
  print @Message$       : ' Replaced by: ? "Hello world!"
  print len(@Message$)  : ' Replaced by: ? 12
  poke @PCOLR0+2, $1F   : ' Replaced by: POKE 706,31
```

### `$incbin` directive.

This directive allows including data from a binary file to a new string
definition. The content of the file is read at parsing time and the full
content is stored in the define. The syntax of the directive is:

  $incbin *defineName$* , "*fileName*"

This is an example usage of the `$incbin` directive:

```
  $options +optimize
  $incbin asmBin$, "myasm.bin"

  asmRut = adr( @asmBin$ )  : ' Store address in variable to use multiple times.
  ? usr(asmRut, 1, 2)       : ' Call routine. Should be relocatable and less than 242 bytes.
```

Limitations and Incompatibilities
---------------------------------

There are some incompatibilities in the way the source is interpreted with the
standard TurboBasic XL and Atari Basic parsers:

- As the ASCII LF character (hexadecimal $10) is interpreted as end of line in
  addition to the ATASCI EOL (hexadecimal $9B). This means that in string constants
  and comments the LF character is not accepted.

- The parsing of special characters inside strings means that a valid hexadecimal
  sequence (`\**`, with `*` an hexadecimal number in uppercase) or two backslashes
  are interpreted differently.

- Extra statements after an `IF`/`THEN`/`LineNumber` are converted to a comment.
  In the original, those statements are never executed, so this is not a problem
  with proper code.

- Any string is accepted as a variable name, even if it is already an statement,
  function name or operator.

  The following code is valid:

  ```
    PRINTED = 0     : ' Invalid in Atari Basic, as starts with "PRINT"
    DONE = 3        : ' Invalid in TurboBasic XL, as starts with "DO"
  ```

  This relaxed handling of variable naming creates an incompatibility, as the
  first example above is parsed differently as the standard Atari Basic,
  where it means "`PRINT (ED = 0)`" instead of "`LET PRINTED = 0`".

  Note that currently, even full statements are accepted as variable names,
  but avoid using them as they could produce hard to understand errors.

- In long format listing output, `IF`/`THEN` are converted to `IF`/`ENDIF`
  statements. This introduces an incompatibility with the following code:

  ```
    FOR A = 0 TO 2
      ? "A="; A; " - ";
      IF A <> 0
        ? "1";
        IF A = 1 THEN ELSE
        ? "2";
      ENDIF
      ? " -"
    NEXT A
  ```

  This code should produce the following at output:

  ```
    A=0 - 2 -
    A=1 - 1 -
    A=2 - 12 -
  ```

  After conversion, the `ELSE` is associated with the second `IF` instead
  of the first, giving the wrong result.

- Parsing of `TIME$=` statement allows a space between `TIME$` and the equals
  sign, but in TurboBasic XL this gives an error.


Compilation
-----------

To compile from source, you need `gawk` and `peg`, both are available in any
recent Debian or Ubuntu Linux distro, install with:

    apt-get install gawk peg

To compile, simply type `make` in the sources folder, a folder `build` will be
created with the executable program inside.


