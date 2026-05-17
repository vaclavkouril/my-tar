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
print_error_eof()
{
	print_error("Unexpected EOF in archive");
	print_error("Error is not recoverable: exiting now");
}

static void
print_warn_lone_empty_block(const unsigned long long b_num)
{
	fprintf(stderr, "mytar: A lone zero block at %llu\n", b_num);
}

static void
print_unsupp_typeflag(const unsigned char typeflag)
{
	fprintf(stderr, "mytar: Unsupported header type: %d\n", typeflag);
}

static void
print_hdr_name(const struct posix_header *hdr)
{
	printf("%s\n", hdr->name);
}

static void
print_file_not_found_archive(const char *filename)
{
	fprintf(stderr, "mytar: %s: Not found in archive\n", filename);
}

static void
print_exist_prev_err()
{
	print_error("Exiting with failure status due to previous errors");
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

	for (int i = 1; i < argc; ++i) {
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
parse_number(const char *field, size_t n, uint64_t *result)
{
	*result = 0;

	/* star extension type number */
	if (((unsigned char)field[0] & 0x80) != 0) {
		/* removing the first mark bit */
		*result = (uint64_t)((unsigned char)field[0] & 0x7f);

		for (size_t i = 1; i < n; ++i) {
			*result <<= 8;
			*result |= (unsigned char)field[i];
		}

		return (1);
	}

	/* standard octet type */
	for (size_t i = 0; i < n; ++i) {
		/* trailing pad */
		if (field[i] == '\0' || field[i] == ' ') {
			break;
		}

		if (field[i] < '0' || field[i] > '7') {
			return (0);
		}

		*result <<= 3;
		*result += (unsigned char)field[i]  - '0';
	}
	return (1);
}

static int
read_block(FILE *fp, unsigned char block[BLOCKSIZE])
{
	size_t n = fread(block, 1, BLOCKSIZE, fp);

	if (n == 0) {
		return (0);
	}

	if (n != BLOCKSIZE) {
		return (-1);
	}

	return (1);
}

static int
skip_data(FILE *fp, uint64_t size)
{
	unsigned char b[BLOCKSIZE];
	uint64_t blocks = (size + BLOCKSIZE - 1) / BLOCKSIZE;

	for (uint64_t i = 0; i < blocks; ++i) {
		if (read_block(fp, b) != 1) {
			return (2);
		}
	}

	return (0);
}

static int
is_empty_block(const unsigned char block[BLOCKSIZE])
{
	for (int i = 0; i < BLOCKSIZE; ++i) {
		if (block[i] != 0) {
			return (0);
		}
	}
	return (1);
}

static int
is_supported_header_type(struct posix_header *hdr)
{
	return (hdr->typeflag == REGTYPE || hdr->typeflag == AREGTYPE);
}

static int
str_found_in_array(const char *s, int *found, char **arr, size_t n)
{
	for (size_t i = 0; i < n; ++i) {
		if (strcmp(s, arr[i]) == 0) {
			found[i] = 1;
			return (1);
		}
	}
	return (0);
}

static int
list_archive(FILE *fp, struct options *opt)
{
	unsigned char b[BLOCKSIZE];
	uint64_t size;
	struct posix_header *hdr;
	uint64_t block_num = 0;
	int read_return;
	int *found = NULL;
	int rc = 0;
	uint64_t not_found_num = 0;

	if (opt->file_count > 0) {
		found = calloc((size_t)opt->file_count, sizeof (int));
		if (found == NULL) {
			print_error("memory exhausted");
			return (2);
		}
	}


	while (1) {
		read_return = read_block(fp, b);
		if (read_return == 0) {
			rc = 0;
			break;
		}
		if (read_return < 0) {
			print_error_eof();
			rc = 2;
			break;
		}

		++block_num;

		if (is_empty_block(b)) {
			read_return = read_block(fp, b);
			if (read_return == 0) {
				print_warn_lone_empty_block(block_num);
				rc = 0;
				break;
			}

			if (read_return < 0) {
				print_error_eof();
				rc = 2;
				break;
			}

			if (is_empty_block(b)) {
				rc = 0;
				break;
			}

			/* empty block in the middle of data */
			print_warn_lone_empty_block(block_num);
			rc = 0;
			break;
		}

		hdr = (struct posix_header *)b;

		if (!is_supported_header_type(hdr)) {
			print_unsupp_typeflag(hdr->typeflag);
			rc = 2;
			break;
		}

		if (opt->file_count) {
			if (str_found_in_array(hdr->name,  found,
					opt->files, opt->file_count)) {
				print_hdr_name(hdr);
			}
		} else {
			print_hdr_name(hdr);
		}

		if (parse_number(hdr->size, sizeof (hdr->size), &size) != 1) {
			print_error("Malformed tar header");
			rc = 2;
			break;
		}
		if (skip_data(fp, size) != 0) {
			print_error_eof();
			rc = 2;
			break;
		}

		block_num += (size + BLOCKSIZE - 1) / BLOCKSIZE;

	}

	if (rc == 0 && opt-> file_count > 0) {
		for (int i = 0; i < opt->file_count; ++i) {
			if (!found[i]) {
				print_file_not_found_archive(opt->files[i]);
				++not_found_num;
			}
		}
	}
	if (not_found_num > 0) {
		print_exist_prev_err();
		rc = 2;
	}

	free(found);
	return (rc);
}

static int
extract_archive(FILE *fp, struct options *opt)
{
	/* TODO: -x used */
	(void) fp;
	(void) opt;
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
