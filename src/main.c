/*
 *  Basic Parser - TurboBasic XL compatible parsing and transformation tool.
 *  Copyright (C) 2015 Daniel Serpell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program.  If not, see <http://www.gnu.org/licenses/>
 */
#include <stdio.h>
#include "parser.h"
#include "program.h"
#include "lister.h"
#include "vars.h"
#include "dbg.h"
#include "dmem.h"
#include "baswriter.h"
#include "version.h"
#include "optimize.h"
#include "convertbas.h"
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#ifndef __WIN32
# include <sys/stat.h>
#else
# include <windows.h>
#endif

int do_debug = 1;

// Called from parser
static enum output_type out_type = out_binary;
enum output_type get_output_type(void)
{
    return out_type;
}

static void show_vars_stats(int renamed, int bin)
{
    unsigned i;
    fprintf(stderr,"Variables information:\n");
    vars *v = pgm_get_vars( parse_get_current_pgm() );
    for(i=0; i<vtMaxType; i++)
    {
        int n = vars_get_count(v,i);
        if( n != 0 )
        {
            fprintf(stderr," Variables of type %s: %d\n", var_type_name(i), n);
            if( (bin || renamed) && do_debug > 1 )
                vars_show_summary(v,i,bin);
        }
    }
}

static char *get_out_filename(const char *inFname, const char *output, const char *ext)
{
    if( output )
        return dstrdup(output);

    char * out = dmalloc(strlen(inFname) + 1 + strlen(ext));
    strcpy(out,inFname);
    char *p = strrchr(out, '.');
    if( p )
        *p = 0;
    strcat(out,ext);
    return out;
}

// Returns 1 if both files are the same
static int is_same_file(const char *p1, const char *p2)
{
#ifndef __WIN32
    // Unix, use "stat"
    struct stat s1, s2;
    if( stat(p1, &s1) == -1 )
        return 0;
    if( stat(p2, &s2) == -1 )
        return 0;
    return s1.st_dev == s2.st_dev && s1.st_ino == s2.st_ino;
#else
    // Windows, use GetFileInformationByHandle
    HANDLE h1, h2;
    h1 = CreateFile(p1, 0, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if( h1 == INVALID_HANDLE_VALUE )
        return 0;
    h2 = CreateFile(p2, 0, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if( h2 == INVALID_HANDLE_VALUE )
    {
        CloseHandle(h1);
        return 0;
    }
    // Retrieve information
    BY_HANDLE_FILE_INFORMATION s1, s2;
    if( GetFileInformationByHandle(h1, &s1) && GetFileInformationByHandle(h2, &s2) )
    {
        CloseHandle(h1);
        CloseHandle(h2);
        return s1.dwVolumeSerialNumber == s2.dwVolumeSerialNumber &&
               s1.nFileIndexHigh == s2.nFileIndexHigh &&
               s1.nFileIndexLow == s2.nFileIndexLow;
    }
    CloseHandle(h1);
    CloseHandle(h2);
    return 0;
#endif
}

static void cmd_help(const char *prog, const char *msg)
{
    if( msg )
        fprintf(stderr, "%s: Error, %s.\n", prog, msg);
    fprintf(stderr, "Try %s -h for help.\n", prog);
    exit(EXIT_FAILURE);
}

static void print_header(void)
{
    fprintf(stderr, "TurboBasic XL parser tool - version " GIT_VERSION "\n"
            "https://github.com/dmsc/tbxl-parser\n\n");
}

int main(int argc, char **argv)
{
    FILE *outFile;
    int do_optimize = 0;
    int opt;
    int do_conv_ascii = 0;
    char *output = 0;
    const char *extension = 0;
    int max_opt_len = 0;
    int max_line_len = 120;
    int max_bin_len = 255;
    int bin_variables = 0;
    int keep_comments = 0;
    enum parser_dialect parser_dialect = parser_dialect_turbo;

    while ((opt = getopt(argc, argv, "hkaAbvsqlco:n:fxO")) != -1)
    {
        switch (opt)
        {
            case 'q':
                do_debug = 0;
                break;
            case 'v':
                do_debug++;
                break;
            case 'l':
                out_type = out_long;
                break;
            case 'b':
                out_type = out_binary;
                break;
            case 's':
                out_type = out_short;
                break;
            case 'a':
                do_conv_ascii = 1;
                break;
            case 'A':
                parser_dialect = parser_dialect_atari;
                break;
            case 'f':
                bin_variables = 1;
                break;
            case 'x':
                bin_variables = -1;
                break;
            case 'k':
                keep_comments = 1;
                break;
            case 'c':
                output = dstrdup("-");
                break;
            case 'o':
                if( optarg[0] == '.' )
                    extension = optarg;
                else
                    output = dstrdup(optarg);
                break;
            case 'O':
                if( optind < argc )
                {
                    const char *opt = argv[optind];
                    int set = 0, force = 0, level;
                    optind ++;
                    if( !strcmp(opt, "help") )
                    {
                        // List all optimization options and exit
                        print_header();
                        optimize_list_options();
                        exit(EXIT_FAILURE);
                    }
                    // Option could start with "-" or "+":
                    if( opt[0] == '-' )
                    {
                        opt ++;
                        set = -1;
                    }
                    else if( opt[0] == '+' )
                    {
                        opt ++;
                        force = 1;
                    }

                    level = optimize_option(opt);
                    if( level )
                        do_optimize = set ^ ((set ^ do_optimize) | level);
                    else if( force )
                    {
                        cmd_help(argv[0], "optimization option invalid, use -O help");
                    }
                    else
                    {
                        // Not an optimize option, go back and
                        // assume "set all optimizations".
                        do_optimize = optimize_all();
                        optind --;
                    }
                }
                else
                    do_optimize = optimize_all();
                break;
            case 'n':
                max_opt_len = atoi(optarg);
                break;
            case 'h':
                print_header();
                fprintf(stderr, "Usage: %s [options] filename\n"
                                "\t-l  Output long (readable) program.\n"
                                "\t-b  Output binary (.BAS) program. (default)\n"
                                "\t-s  Output short listing program.\n"
                                "\t-n  In short listing, sets the max line length before splitting (%d),\n"
                                "\t    and in binary output limit binary line bytes (%d).\n"
                                "\t-f  Output full (long) variable names in binary output.\n"
                                "\t-k  Keeps comments in binary output.\n"
                                "\t-x  Makes binary output protected (un-listable).\n"
                                "\t-a  In long output, convert comments to pure ASCII.\n"
                                "\t-A  Parse and outputs Atari Basic dialect instead of TurboBasicXL.\n"
                                "\t-v  Shows more parsing information (verbose mode).\n"
                                "\t-q  Don't show parsing information (quiet mode).\n"
                                "\t-o  Sets the output file name or extension (if starts with a dot).\n"
                                "\t-c  Output to standard output instead of a file.\n"
                                "\t-O  Optimize the parsed program. An optional argument with '+' or '-'\n"
                                "\t    enables/disables specific optimization. Use -O help for a list of\n"
                                "\t    all available options.\n"
                                "\t-h  Shows help and exit.\n",
                        argv[0], max_line_len, max_bin_len);
                exit(EXIT_FAILURE);
            default:
                cmd_help(argv[0], 0);
        }
    }

    if (optind >= argc)
        cmd_help(argv[0], "expected at least one input file");

    if( output && strcmp(output,"-") && optind+1  != argc )
        cmd_help(argv[0], "when setting output file, only one input file should be supplied");

    if( output && extension )
        cmd_help(argv[0], "only one of output file name or extension should be supplied");

    if( !extension )
        extension = (out_type == out_binary) ? ".bas" : ".lst";

    if( max_opt_len )
    {
        // Option for max line length, interpret differently depending on listing or BAS output.
        switch( out_type )
        {
            case out_binary:
                if( max_opt_len < 16 || max_opt_len > 255 )
                    cmd_help(argv[0], "maximum binary line length invalid");
                max_bin_len = max_opt_len;
                break;
            case out_short:
                if( max_opt_len < 16 || max_opt_len > 511 )
                    cmd_help(argv[0], "maximum line length invalid");
                else if( max_opt_len > 255 )
                    fprintf(stderr,
                            "WARNING: lines of length of %d can't be used in original BASIC.\n",
                            max_opt_len);
                max_line_len = max_opt_len;
                break;
            default:
                cmd_help(argv[0], "invalid option '-n' on long listing mode");
                break;
        }
    }

    int all_ok = 1;
    for( ; optind<argc; optind++)
    {
        const char *inFname = argv[optind];

        // Get list output
        char *outFname = get_out_filename( inFname, output, extension );
        if( is_same_file(inFname, outFname) )
        {
            err_print(inFname, 0, "output file '%s' is the same as input.\n", outFname);
            exit(EXIT_FAILURE);
        }
        if( output && strcmp( output, "-" ) )
        {
            free(output);
            output = 0;  // Only use on first file
        }

        info_print(inFname, 0, "parsing to '%s'\n", outFname);

        // Parse input file
        parse_init(inFname);
        parser_set_optimize(0);
        parser_add_optimize(do_optimize, 1);
        parser_set_dialect(parser_dialect);
        int ok = parse_file(inFname);

        // Convert to TurboBasic compatible if output is BAS or short LST
        if( ok && (out_type == out_short || out_type == out_binary) )
            ok = !convert_to_turbobas(parse_get_current_pgm(), keep_comments);

        // Run the optimizer if specified by the user or not in long output
        if( ok && (out_type != out_long || parser_get_optimize()) )
            ok = !optimize_program(parse_get_current_pgm(),parser_get_optimize());

        // Update "all_ok" variable
        all_ok = ok ? all_ok : 0;

        // Write output if parse was ok or if writing long output
        if( ok || out_type == out_long )
        {
            if( !ok )
            {
                fprintf(stderr,"\n"
                               "%s: errors detected but generating long list anyway,\n"
                               "%s: the output listing will contain errors.\n",
                               inFname, inFname);
            }
            else if( do_debug )
                fprintf(stderr, "%s: parsing file complete.\n", inFname);

            // Open output file
            if( strcmp( outFname, "-" ) )
                outFile = fopen(outFname,"wb");
            else
                outFile = stdout;

            if( !outFile )
            {
                fprintf(stderr,"%s: error %s\n", outFname, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // Reassign short variable names if requested
            if( out_type == out_short && bin_variables > 0 )
                vars_assign_short_names( pgm_get_vars( parse_get_current_pgm() ) );

            if( do_debug )
                show_vars_stats(out_type == out_short ||
                                (out_type == out_binary && !bin_variables),
                                bin_variables < 0);

            // Write output
            int err = 0;
            if( out_type == out_short )
                err = lister_list_program_short(outFile, parse_get_current_pgm(), max_line_len);
            else if( out_type == out_long )
                err = lister_list_program_long(outFile, parse_get_current_pgm(), do_conv_ascii);
            else if( out_type == out_binary )
                err = bas_write_program(outFile, parse_get_current_pgm(), bin_variables, max_bin_len);

            // Remember if there was an error:
            all_ok = err ? 0 : all_ok;

            if( outFile != stdout )
                fclose(outFile);

        }
        free(outFname);
        program_delete( parse_get_current_pgm() );

        if( do_debug )
            fprintf(stderr, "\n");
    }

    if( output )
        free(output);

    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

