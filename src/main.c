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
#include "baswriter.h"
#include "version.h"
#include "optimize.h"
#include "convertbas.h"
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

int do_debug = 1;

static void show_vars_stats(void)
{
    unsigned i;
    fprintf(stderr,"Variables information:\n");
    vars *v = pgm_get_vars( parse_get_current_pgm() );
    for(i=0; i<vtMaxType; i++)
    {
        int n = vars_get_count(v,i);
        if( n != 0 )
            fprintf(stderr," %d variables of type %s\n", n, var_type_name(i));
    }
}

static char *get_out_filename(const char *inFname, const char *output, const char *ext)
{
    if( output )
        return strdup(output);

    char * out = malloc(strlen(inFname) + 1 + strlen(ext));
    strcpy(out,inFname);
    char *p = strrchr(out, '.');
    if( p )
        *p = 0;
    strcat(out,ext);
    return out;
}

int main(int argc, char **argv)
{
    FILE *outFile;
    int do_optimize = 0;
    int opt;
    enum {
        out_short,
        out_long,
        out_binary } out_type = out_binary;
    int do_conv_ascii = 0;
    char *output = 0;
    const char *extension = 0;
    int max_line_len = 120;
    int bin_variables = 0;

    while ((opt = getopt(argc, argv, "habvsqlco:n:fxO")) != -1)
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
            case 'f':
                bin_variables = 1;
                break;
            case 'x':
                bin_variables = -1;
                break;
            case 'c':
                output = strdup("-");
                break;
            case 'o':
                if( optarg[0] == '.' )
                    extension = optarg;
                else
                    output = strdup(optarg);
                break;
            case 'O':
                do_optimize = 1;
                break;
            case 'n':
                max_line_len = atoi(optarg);
                if( max_line_len < 16 || max_line_len > 355 )
                {
                    fprintf(stderr, "Error, maximum line length (%s) invalid.\n"
                                    "try %s -h for help.\n", optarg, argv[0]);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
            default:
                fprintf(stderr, "TurboBasic XL parser tool - version " GIT_VERSION "\n"
                                "https://github.com/dmsc/tbxl-parser\n"
                                "\n"
                                "Usage: %s [options] filename\n"
                                "\t-l  Output long (readable) program.\n"
                                "\t-b  Output binary (.BAS) program. (default)\n"
                                "\t-s  Output short listing program.\n"
                                "\t-n  In short listing, sets the max line length before splitting (%d).\n"
                                "\t-f  Output full (long) variable names in binary output.\n"
                                "\t-x  Makes binary output protected (un-listable).\n"
                                "\t-a  In long output, convert comments to pure ASCII.\n"
                                "\t-v  Shows more parsing information (verbose mode).\n"
                                "\t-q  Don't show parsing information (quiet mode).\n"
                                "\t-o  Sets the output file name or extension (if starts with a dot).\n"
                                "\t-c  Output to standard output instead of a file.\n"
                                "\t-O  Defaults to run the optimizer in the parsed program.\n"
                                "\t-h  Shows help and exit.\n",
                        argv[0], max_line_len);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Expected argument after options\n");
        exit(EXIT_FAILURE);
    }

    if( output && strcmp(output,"-") && optind+1  != argc )
    {
        fprintf(stderr, "When setting output file, only one input file should be supplied\n");
        exit(EXIT_FAILURE);
    }

    if( output && extension )
    {
        fprintf(stderr, "Only one of output file name or extension should be supplied.\n");
        exit(EXIT_FAILURE);
    }

    if( !extension )
        extension = (out_type == out_binary) ? ".bas" : ".lst";

    int all_ok = 1;
    for( ; optind<argc; optind++)
    {
        const char *inFname = argv[optind];

        // Get list output
        char *outFname = get_out_filename( inFname, output, extension );
        if( !strcmp(inFname, outFname) )
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
        parser_set_optimize(do_optimize);
        int ok = parse_file(inFname);

        // Convert to TurboBasic compatible if output is BAS or short LST
        if( ok && (out_type == out_short || out_type == out_binary) )
            ok = !convert_to_turbobas(parse_get_current_pgm());

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

            // Run the optimizer if specified by the user
            if( ok && parser_get_optimize() )
                optimize_program(parse_get_current_pgm(),parser_get_optimize());

            if( do_debug )
                show_vars_stats();

            // Write output
            int err = 0;
            if( out_type == out_short )
                err = lister_list_program_short(outFile, parse_get_current_pgm(), max_line_len);
            else if( out_type == out_long )
                err = lister_list_program_long(outFile, parse_get_current_pgm(), do_conv_ascii);
            else if( out_type == out_binary )
                err = bas_write_program(outFile, parse_get_current_pgm(), bin_variables);

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

