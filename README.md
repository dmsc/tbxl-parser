TurboBasic XL parser tool
-------------------------

This program parses and tokenizes a _TurboBasic XL_ listing in a flexible format
and produces any of three outputs:

- A minimized listing, replacing variable names with single letters, using
  abbreviations, removing spaces and using Atari end of lines.

  This is the default operating mode.

- A pretty printed expanded listing, with one statement per line and
  indentation, and standard ASCII line endings.

  Note that this format can be read back again.

  This mode is selected with the `-l` command line switch.

- A tokenized binary file, directly loadable in the original _TurboBasic XL_
  interpreter.  This mode also replaces variables with single letters.

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
  followed by am hexadecimal number, (i.e., `"\00\A0"`), this allows editing on
  any ASCII editor.

- Comments can be started by `'` in addition to the _TurboBasic_ `.`, `--` or
  `rem`.

- The input is case insensitive (uppercase, lowercase and mixed case is
  supported).


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

- `-a`  In long output, replace Atari characters in comments with
  approximating characters.

- `-v`  Shows more parsing information, like name of renamed variables.
  (verbose mode)

- `-q`  Don't show any parsing output, only errors.  (quiet mode)

- `-o`  Sets the output file name.  By default, the output is the names of the
  input with ".lst" extension.

- `-c`  Output to standard output instead of a file.

- `-h`  Shows help and exit.


Compilation
-----------

To compile from source, you need `gawk` and `peg`, both are available in any
recent Debian or Ubuntu Linux distro, install with:

    apt-get install gawk peg

To compile, simply type `make` in the sources folder, a folder `build` will be
created with the executable program inside.


