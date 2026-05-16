#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define	BLOCKSIZE	512

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

#define	TMAGIC		"ustar"
#define	TMAGLEN		6
#define	TVERSION	"00"
#define	TVERSLEN	2

#define	REGTYPE		'0'
#define	AREGTYPE	'\0'
#define	DIRTYPE		'5'


struct options {
	int t_flag;
	int x_flag;
	int v_flag;
	const char *archive_name;
	int file_count;
	char **files;
};

enum arg_type {
	ARG_UNKNOWN,
	ARG_OPTION_T,
	ARG_OPTION_X,
	ARG_OPTION_V,
	ARG_OPTION_F,
	ARG_FILENAME
};

static void
print_error(const char *msg)
{
	fprintf(stderr, "mytar: %s\n", msg);
}

static void
print_error_arg(const char *msg, const char *arg)
{
	fprintf(stderr, "mytar: %s%s\n", msg, arg);
}

static void
print_error_file(const char *file, const char *msg)
{
	fprintf(stderr, "mytar: %s: %s\n", file, msg);
}

static int
type_arg_assign(const char *arg)
{
	if (strcmp(arg, "-t") == 0) {
		return (ARG_OPTION_T);
	}

	if (strcmp(arg, "-x") == 0) {
		return (ARG_OPTION_X);
	}

	if (strcmp(arg, "-v") == 0) {
		return (ARG_OPTION_V);
	}

	if (strcmp(arg, "-f") == 0) {
		return (ARG_OPTION_F);
	}

	if (arg[0] == '-') {
		return (ARG_UNKNOWN);
	}

	return (ARG_FILENAME);
}


static int
parse_args(int argc, char **argv, struct options *opt)
{
	opt->t_flag = 0;
	opt->x_flag = 0;
	opt->v_flag = 0;
	opt->archive_name = NULL;
	opt->file_count = 0;

	if (argc < 2) {
		print_error("need at least one option");
		return (2);
	}

	opt->files = malloc((size_t) argc * sizeof (char *));
	if (opt->files == NULL) {
		print_error("memory exhausted");
		return (2);

	}

	/* archive name always right after -f option */
	int expect_archive_nxt = 0;

	for (int i = 1; i < argc; i++) {
		int type = type_arg_assign(argv[i]);

		if (expect_archive_nxt) {
			opt->archive_name = argv[i];
			expect_archive_nxt = 0;
			continue;
		}

		switch (type) {
			case ARG_OPTION_T:
				opt->t_flag = 1;
				break;

			case ARG_OPTION_X:
				opt->x_flag = 1;
				break;

			case ARG_OPTION_V:
				opt->v_flag = 1;
				break;

			case ARG_OPTION_F:
				expect_archive_nxt = 1;
				break;

			case ARG_FILENAME:
				opt->files[opt->file_count++] = argv[i];
				break;

			default:
				print_error_arg("Unknown option: ", argv[i]);
				free(opt->files);
				return (2);
		}
	}

	if (expect_archive_nxt) {
		print_error("option -f requires an argument");
		free(opt->files);
		return (2);
	}

	if (opt->archive_name == NULL) {
		print_error("option -f is required");
		free(opt->files);
		return (2);
	}

	if (opt->t_flag == opt->x_flag) {
		print_error("specify exactly one of -t or -x");
		free(opt->files);
		return (2);
	}

	return (0);
}

static int
list_archive(FILE *fp, struct options *opt)
{
	return (0);
}

static int
extract_archive(FILE *fp, struct options *opt)
{
	/* TODO: -x used */
	(void)fp;
	(void)opt;
	return (0);
}

int
main(int argc, char **argv)
{
	struct options opt;
	FILE *fp;
	int rc;

	if (parse_args(argc, argv, &opt) != 0) {
		return (2);
	}

	fp = fopen(opt.archive_name, "rb");
	if (fp == NULL) {
		fprintf(stderr, "mytar: %s: Cannot open: %s\n",
			opt.archive_name, strerror(errno));
		free(opt.files);
		return (2);
	}

	if (opt.t_flag) {
		rc = list_archive(fp, &opt);
	} else {
		/* TODO: 2nd phase implementation */
		rc = extract_archive(fp, &opt);
	}

	fclose(fp);
	free(opt.files);

	return (rc);
}
