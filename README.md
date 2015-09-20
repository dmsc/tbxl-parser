TurboBasic XL parser tool
-------------------------

This program parses and tokenizes a _TurboBasic XL_ listing in a flexible format
and produces any of three outputs:

- A minimized listing, replacing variable names with single letters, using
  abbreviations, removing spaces and using Atari end of lines.

  This is the default operating mode.

- A pretty printed expanded listing, with one statement per line and
  indentation, and standard ASCII line endings.

  Note that this format can be read back again, but some statements are
  transformed in the process, this can lead to problems in non-standard
  IF/THEN constructs.

  Currently, `IF`/`THEN` with statements after the `THEN` are converted to
  multi-line `IF`/`ENDIF` statements.

  This mode is selected with the `-l` command line switch.

- A tokenized binary file, directly loadable in the original _TurboBasic XL_
  interpreter.  This mode also replaces variables with single letters by
  default, but with the `-f` option writes the full variable names and
  with the `-x` option writes invalid variable names, making the program
  unable to be listed or edited.

  This mode is selected with the `-b` command line switch.


The input listing format is very flexible:

- Standard Atari listing format, with Atari or ASCII end of lines.  The parser
  understand the same abbreviations available in the original interpreter.

- A free format listing, with line numbers separated from the statements.

  In this format, not each line needs line number, only lines that are target
  to `GOTO` / `GOSUB` / `THEN` needs them.  If you use only labels, no line
  numbers are needed.

  Also, line numbers can appear alone in a line, for better readability.

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

- `-l`  Output long (readable) listing instead of minimized.

- `-b`  Output a binary tokenized file instead of a listing. The  default
  output file name is the same as input with `.bas` extension added.

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


