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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

int do_debug = 1;

static void show_vars_stats()
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

    char * out = malloc(strlen(inFname) + 4);
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
    int opt;
    enum {
        out_short,
        out_long,
        out_binary } out_type = out_short;
    int do_conv_ascii = 0;
    const char *output = 0;
    int max_line_len = 120;
    int bin_variables = 0;

    while ((opt = getopt(argc, argv, "habvqlco:n:fx")) != -1)
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
                output = "-";
                break;
            case 'o':
                output = strdup(optarg);
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
                                "Usage: %s [-h] [-v] [-n len] [-l] [-a] [-c] [-o output] filename\n"
                                "\t-l  Output long (readable) program.\n"
                                "\t-b  Output binary (.BAS) program.\n"
                                "\t-f  Output full (long) variable names in binary output.\n"
                                "\t-x  Makes binary output protected (un-listable).\n"
                                "\t-a  In long output, convert comments to pure ASCII.\n"
                                "\t-v  Shows more parsing information (verbose mode).\n"
                                "\t-q  Don't show parsing information (quiet mode).\n"
                                "\t-o  Sets the output file name, instead of default one.\n"
                                "\t-c  Output to standard output instead of a file.\n"
                                "\t-n  Sets the max line length before splitting (%d).\n"
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
        fprintf(stderr, "When seting output file, only one input file should be supplied\n");
        exit(EXIT_FAILURE);
    }

    int all_ok = 1;
    for( ; optind<argc; optind++)
    {
        const char *inFname = argv[optind];

        // Get list output
        char *outFname = get_out_filename( inFname, output,
                                           (out_type == out_binary) ? ".bas" : ".lst" );
        if( !strcmp(inFname, outFname) )
        {
            err_print("%s:%s: output file is the same as input.\n", inFname, outFname);
            exit(EXIT_FAILURE);
        }
        if( output && strcmp( output, "-" ) )
            output = 0;  // Only use on first file

        info_print("parse file '%s' to '%s'\n", inFname, outFname);

        // Parse input file
        int ok = parse_file(inFname);

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
                err_print("%s: error %s\n", outFname, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if( do_debug )
                show_vars_stats();

            // Write output
            if( out_type == out_short )
                lister_list_program_short(outFile, parse_get_current_pgm(), max_line_len);
            else if( out_type == out_long )
                lister_list_program_long(outFile, parse_get_current_pgm(), do_conv_ascii);
            else if( out_type == out_binary )
                bas_write_program(outFile, parse_get_current_pgm(), bin_variables);

            if( outFile != stdout )
                fclose(outFile);

        }
        free(outFname);
        program_delete( parse_get_current_pgm() );

        if( do_debug )
            fprintf(stderr, "\n");
    }

    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

