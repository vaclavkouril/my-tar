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

typedef int (*fn_file_handler)(FILE *archive, struct posix_header *hdr,
				uint64_t size, struct options *opt, int *found);

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
args_error(struct options *opt, const char *msg)
{
	print_error(msg);
	free(opt->files);
	return (2);
}

static int
check_options(struct options *opt, int expect_archive)
{
	if (expect_archive) {
		return (args_error(opt, "option -f requires an argument"));
	}

	if (opt->archive_name == NULL) {
		return (args_error(opt, "option -f is required"));
	}

	if (opt->t_flag == opt->x_flag) {
		return (args_error(opt, "specify exactly one of -t or -x"));
	}

	return (0);
}

static void
init_options(struct options *opt)
{
	opt->t_flag = 0;
	opt->x_flag = 0;
	opt->v_flag = 0;
	opt->archive_name = NULL;
	opt->file_count = 0;
	opt->files = NULL;
}

static int
alloc_selected_files(struct options *opt, int argc)
{
	opt->files = malloc((size_t)argc * sizeof (char *));
	if (opt->files == NULL) {
		print_error("memory exhausted");
		return (2);
	}

	return (0);
}

static int
handle_arg(struct options *opt, char *arg, int *expect_archive)
{
	int type;

	if (*expect_archive) {
		opt->archive_name = arg;
		*expect_archive = 0;
		return (0);
	}

	type = type_arg_assign(arg);

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
			*expect_archive = 1;
			break;

		case ARG_FILENAME:
			opt->files[opt->file_count++] = arg;
			break;

		default:
			print_error_arg("Unknown option: ", arg);
			return (2);
	}

	return (0);
}

static int
parse_args(int argc, char **argv, struct options *opt)
{
	init_options(opt);

	/* archive name always right after -f option */
	int expect_archive = 0;

	if (argc < 2) {
		print_error("need at least one option");
		return (2);
	}

	if (alloc_selected_files(opt, argc) != 0) {
		return (2);
	}

	for (int i = 1; i < argc; ++i) {
		if (handle_arg(opt, argv[i], &expect_archive) != 0) {
			free(opt->files);
			return (2);
		}
	}

	return (check_options(opt, expect_archive));
}

static int
parse_star_size(const char *field, size_t n, uint64_t *result)
{
	*result = 0;

	/* masking away first mark bit */
	*result = (uint64_t)((unsigned char)field[0] & 0x7f);

	for (size_t i = 1; i < n; ++i) {
		*result <<= 8;
		*result |= (unsigned char)field[i];
	}
	return (1);
}

static int
parse_octal_size(const char *field, size_t n, uint64_t *result)
{
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

/*
 * Header numeric fields are normally stored as octal ASCII. GNU/star archives
 * may use base-256 encoding for large values. The first bit of the first byte
 * decides that representation.
 */
static int
parse_hdr_size(const char *field, size_t n, uint64_t *result)
{
	*result = 0;

	if (((unsigned char)field[0] & 0x80) != 0) {
		return (parse_star_size(field, n, result));
	}
	return (parse_octal_size(field, n, result));
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

/*
 * Tar file data is padded to 512-byte blocks.  We always read whole blocks,
 * but write only the actual file size.  If out is NULL, data is only skipped.
 */
static int
copy_data(FILE *in, FILE *out, uint64_t size)
{
	unsigned char b[BLOCKSIZE];
	uint64_t remaining;
	size_t to_write;

	remaining = size;

	while (remaining > 0) {
		if (read_block(in, b) != 1) {
			return (2);
		}

		to_write = remaining > BLOCKSIZE ?
			BLOCKSIZE :
			(size_t)remaining;

		if (out != NULL && fwrite(b, 1, to_write, out) != to_write) {
			return (2);
		}

		remaining -= to_write;
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

/*
 * A tar archive normally ends with two zero blocks. GNU tar accepts archives
 * missing both blocks silently and warns when only one zero block is present.
 */
static int
handle_empty_block(FILE *fp, uint64_t block_num)
{
	unsigned char b[BLOCKSIZE];
	int read_return;

	read_return = read_block(fp, b);
	if (read_return == 0) {
		print_warn_lone_empty_block(block_num);
		return (0);
	}

	if (read_return < 0) {
		print_error_eof();
		return (2);
	}

	if (is_empty_block(b)) {
		return (0);
	}

	/* empty block in the middle of data */
	print_warn_lone_empty_block(block_num);
	return (0);
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
report_nonfound(struct options *opt, int *found)
{
	int rc;

	rc = 0;
	for (int i = 0; i < opt->file_count; ++i) {
		if (!found[i]) {
			print_file_not_found_archive(opt->files[i]);
			rc = 2;
		}
	}

	if (rc != 0) {
		print_exist_prev_err();
	}

	return (rc);
}

static int
should_process_file(const char *name, struct options *opt, int *found)
{
	if (opt->file_count == 0) {
		return (1);
	}

	return (str_found_in_array(name, found,
	    opt->files, (size_t)opt->file_count));
}

static int
read_header_size(struct posix_header *hdr, uint64_t *size)
{
	if (parse_hdr_size(hdr->size, sizeof (hdr->size), size) != 1) {
		print_error("Malformed tar header");
		return (2);
	}

	return (0);
}

static int
skip_file_data(FILE *fp, uint64_t size)
{
	if (copy_data(fp, NULL, size) != 0) {
		print_error_eof();
		return (2);
	}
	return (0);
}

static FILE *
open_file(const char *filename, const char *mode)
{
	FILE *fp;

	fp = fopen(filename, mode);
	if (fp == NULL) {
		fprintf(stderr, "mytar: %s: Cannot open: %s\n",
		    filename, strerror(errno));
	}

	return (fp);
}

static int
list_handler(FILE *archive, struct posix_header *hdr,
	    uint64_t size, struct options *opt, int *found)
{
	if (should_process_file(hdr->name, opt, found)) {
		print_hdr_name(hdr);
	}
	return (skip_file_data(archive, size));
}

static int
extract_handler(FILE *archive, struct posix_header *hdr,
		uint64_t size, struct options *opt, int *found)
{
	FILE *out;

	if (!should_process_file(hdr->name, opt, found)) {
		return (skip_file_data(archive, size));
	}

	if (opt->v_flag) {
		print_hdr_name(hdr);
	}

	out = open_file(hdr->name, "wb");
	if (out == NULL) {
		return (2);
	}

	if (copy_data(archive, out, size) != 0) {
		fclose(out);
		print_error_eof();
		return (2);
	}

	fclose(out);
	return (0);
}

static int
is_tar_header(struct posix_header *hdr)
{
	return (memcmp(hdr->magic, TMAGIC, TMAGLEN) == 0);
}

static int *
alloc_found(struct options *opt)
{
	int *found;

	if (opt->file_count == 0) {
		return (NULL);
	}

	found = calloc((size_t)opt->file_count, sizeof (int));
	if (found == NULL) {
		print_error("memory exhausted");
	}

	return (found);
}

static int
check_first_header(struct posix_header *hdr, uint64_t block_num)
{
	if (block_num != 1) {
		return (0);
	}

	if (!is_tar_header(hdr)) {
		print_error("This does not look like a tar archive");
		print_exist_prev_err();
		return (2);
	}

	return (0);
}

static int
process_header_block(FILE *fp, unsigned char b[BLOCKSIZE], struct options *opt,
			int *found, fn_file_handler handler,
			uint64_t block_num, uint64_t *size)
{
	struct posix_header *hdr;

	hdr = (struct posix_header *)b;

	if (check_first_header(hdr, block_num) != 0) {
		return (2);
	}

	if (!is_supported_header_type(hdr)) {
		print_unsupp_typeflag(hdr->typeflag);
		return (2);
	}

	if (read_header_size(hdr, size) != 0) {
		return (2);
	}

	return (handler(fp, hdr, *size, opt, found));
}

enum header_status {
	HDR_OK,
	HDR_END,
	HDR_ERROR
};

static int
read_next_header(FILE *fp, unsigned char b[BLOCKSIZE], uint64_t *block_num)
{
	int read_return;

	read_return = read_block(fp, b);
	if (read_return == 0) {
		return (HDR_END);
	}

	if (read_return < 0) {
		print_error_eof();
		return (HDR_ERROR);
	}

	++(*block_num);

	if (is_empty_block(b)) {
		if (handle_empty_block(fp, *block_num) != 0) {
			return (HDR_ERROR);
		}
		return (HDR_END);
	}

	return (HDR_OK);
}

/*
 * Archive traversal. The handler decides whether a regular file is
 * listed, extracted, or skipped.
 */
static int
process_archive(FILE *fp, struct options *opt, fn_file_handler handler)
{
	unsigned char b[BLOCKSIZE];
	uint64_t size;
	uint64_t block_num = 0;
	int status;
	int *found = NULL;
	int rc = 0;

	found = alloc_found(opt);
	if (opt->file_count > 0 && found == NULL) {
		return (2);
	}

	while (1) {
		status = read_next_header(fp, b, &block_num);

		if (status == HDR_END) {
			break;
		}

		if (status == HDR_ERROR) {
			rc = 2;
			break;
		}

		rc = process_header_block(fp, b, opt,
			    found, handler, block_num, &size);
		if (rc != 0) {
			break;
		}
		block_num += ((size + BLOCKSIZE - 1) / BLOCKSIZE);
	}
	if (rc == 0) {
		rc = report_nonfound(opt, found);
	}

	free(found);
	return (rc);
}

static int
list_archive(FILE *fp, struct options *opt)
{
	return (process_archive(fp, opt, list_handler));
}

static int
extract_archive(FILE *fp, struct options *opt)
{
	return (process_archive(fp, opt, extract_handler));
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

	fp = open_file(opt.archive_name, "rb");

	if (fp == NULL) {
		free(opt.files);
		return (2);
	}
	if (opt.t_flag) {
		rc = list_archive(fp, &opt);
	} else {
		rc = extract_archive(fp, &opt);
	}

	fclose(fp);
	free(opt.files);

	return (rc);
}
