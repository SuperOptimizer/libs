/*
 * as.c - Assembler driver for the free toolchain
 * Usage: free-as input.s -o output.o
 * Pure C89. No external dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* from emit.c */
void assemble(const char *src, const char *outpath);

static char *read_file(const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t nread;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "free-as: cannot open input file: %s\n", path);
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fprintf(stderr, "free-as: out of memory\n");
        fclose(f);
        exit(1);
    }

    nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);

    return buf;
}

static void usage(void)
{
    fprintf(stderr, "Usage: free-as input.s -o output.o\n");
    exit(1);
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    char *src;
    int i;

    /* Handle --version before full arg parsing */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("GNU as (free-as) 2.42\n");
            return 0;
        }
    }

    /* parse command line */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) usage();
            output_path = argv[++i];
        } else if (argv[i][0] != '-') {
            input_path = argv[i];
        } else {
            /* skip unknown flags */
        }
    }

    if (!input_path) {
        fprintf(stderr, "free-as: no input file\n");
        usage();
    }

    if (!output_path) {
        output_path = "a.out.o";
    }

    /* read source */
    src = read_file(input_path);

    /* assemble */
    assemble(src, output_path);

    free(src);
    return 0;
}
