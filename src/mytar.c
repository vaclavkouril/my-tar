#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define BLOCKSIZE 512

/* POSIX ustar header */
struct posix_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

#define TMAGIC   "ustar"
#define TMAGLEN  6
#define TVERSION "00"
#define TVERSLEN 2

#define REGTYPE  '0'
#define AREGTYPE '\0'
#define DIRTYPE  '5'


struct options {
    int t_flag;
    int x_flag;
    int v_flag;
    const char *archive_name;

    int file_count;
    char **files;
};

static int parse_args(int argc, char **argv, struct options *opt);



static int list_archive(FILE* fp, struct options *opt);








static int extract_archive(FILE* fp, struct options *opt);








int main(int argc, char **argv) {
    struct options opt;
    FILE* fp;
    int rc;

    if (parse_args(argc, argv, &opt) != 0){
        return 0;
    }
    
    if (parse_args(argc, argv, &opt) != 0) {
        return 2;
    }

    fp = fopen(opt.archive_name, "rb");
    if (fp == NULL) {
        fprintf(stderr, "mytar: %s: Cannot open: %s\n",
                opt.archive_name, strerror(errno));
        return 2;
    }

    if (opt.t_flag) {
        rc = list_archive(fp, &opt);
    } else {
        rc = extract_archive(fp, &opt);
    }

    fclose(fp);

    return 0;
}
