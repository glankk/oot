#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spec.h"
#include "util.h"

/* We can't use the ALIGN and SUBALIGN expressions on the sections
 * because they might impose a narrower alignment then the input
 * sections require. We align sections manually instead. */
#define SUBALIGN 0x10

static void write_text_sections(FILE *f, const struct Segment *seg)
{
	int i;

	fprintf
	(
		f,
		"\t\t_%sSegmentTextStart = .;\n"
		"\n",
		seg->name
	);

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.text)\n", seg->includes[i].fpath);
		if (seg->includes[i].linkerPadding != 0)
		{
			fprintf
			(
				f,
				"\t\t. += 0x%X;\n",
				seg->includes[i].linkerPadding
			);
		}
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}

	fprintf
	(
		f,
		"\n"
		"\t\t_%sSegmentTextEnd = .;\n"
		"\t\t_%sSegmentTextSize ="
		" ABSOLUTE(_%sSegmentTextEnd - _%sSegmentTextStart);\n",
		seg->name,
		seg->name,
		seg->name, seg->name
	);
}

static void write_data_sections(FILE *f, const struct Segment *seg)
{
	int i;

	fprintf
	(
		f,
		"\t\t_%sSegmentDataStart = .;\n"
		"\n",
		seg->name
	);

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.data)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}

	fprintf
	(
		f,
		"\n"
		"\t\t_%sSegmentDataEnd = .;\n"
		"\t\t_%sSegmentDataSize ="
		" ABSOLUTE(_%sSegmentDataEnd - _%sSegmentDataStart);\n",
		seg->name,
		seg->name,
		seg->name, seg->name
	);
}

static void write_rodata_sections(FILE *f, const struct Segment *seg)
{
	int i;

	fprintf
	(
		f,
		"\t\t_%sSegmentRoDataStart = .;\n"
		"\n",
		seg->name
	);

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.rodata)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t%s (.rodata.str*)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t%s (.rodata.cst*)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}

	fprintf
	(
		f,
		"\n"
		"\t\t_%sSegmentRoDataEnd = .;\n"
		"\t\t_%sSegmentRoDataSize ="
		" ABSOLUTE(_%sSegmentRoDataEnd - _%sSegmentRoDataStart);\n",
		seg->name,
		seg->name,
		seg->name, seg->name
	);
}

static void write_sdata_sections(FILE *f, const struct Segment *seg)
{
	int i;

	fprintf
	(
		f,
		"\t\t_%sSegmentSDataStart = .;\n"
		"\n",
		seg->name
	);

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.sdata)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}

	fprintf
	(
		f,
		"\n"
		"\t\t_%sSegmentSDataEnd = .;\n"
		"\t\t_%sSegmentSDataSize ="
		" ABSOLUTE(_%sSegmentSDataEnd - _%sSegmentSDataStart);\n",
		seg->name,
		seg->name,
		seg->name, seg->name
	);
}

static void write_ovl_sections(FILE *f, const struct Segment *seg)
{
	int i;

	fprintf
	(
		f,
		"\t\t_%sSegmentOvlStart = .;\n"
		"\n",
		seg->name
	);

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.ovl)\n", seg->includes[i].fpath);
	}

	fprintf
	(
		f,
		"\n"
		"\t\t_%sSegmentOvlEnd = .;\n"
		"\t\t_%sSegmentOvlSize ="
		" ABSOLUTE(_%sSegmentOvlEnd - _%sSegmentOvlStart);\n",
		seg->name,
		seg->name,
		seg->name, seg->name
	);
}

static void write_bss_sections(FILE *f, const struct Segment *seg)
{
	int i;

	fprintf
	(
		f,
		"\t\t. = ALIGN(0x%X);\n"
		"\t\t_%sSegmentBssStart = .;\n"
		"\n",
		SUBALIGN,
		seg->name
	);

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.sbss)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}
	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.scommon)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}
	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (.bss)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}
	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf(f, "\t\t%s (COMMON)\n", seg->includes[i].fpath);
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
	}

	fprintf
	(
		f,
		"\n"
		"\t\t. = ALIGN(0x%X);\n"
		"\t\t_%sSegmentBssEnd = .;\n"
		"\t\t_%sSegmentBssSize ="
		" ABSOLUTE(_%sSegmentBssEnd - _%sSegmentBssStart);\n",
		SUBALIGN,
		seg->name,
		seg->name,
		seg->name, seg->name
	);
}

static void write_sections_finale(FILE *f)
{
	fprintf
	(
		f,
		"\t.reginfo          : { *(.reginfo) }\n"
		"\t.pdr              : { *(.pdr) }\n"
		"\n"
		"\t.comment        0 : { *(.comment) }\n"
		"\t.gnu.build.attributes 0 : { *(.gnu.build.attributes) }\n"
		"\n"
		"\t.debug          0 : { *(.debug) }\n"
		"\t.line           0 : { *(.line) }\n"
		"\n"
		"\t.debug_srcinfo  0 : { *(.debug_srcinfo) }\n"
		"\t.debug_sfnames  0 : { *(.debug_sfnames) }\n"
		"\n"
		"\t.debug_aranges  0 : { *(.debug_aranges) }\n"
		"\t.debug_pubnames 0 : { *(.debug_pubnames) }\n"
		"\n"
		"\t.debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }\n"
		"\t.debug_abbrev   0 : { *(.debug_abbrev) }\n"
		"\t.debug_line     0 : { *(.debug_line .debug_line.*"
		" .debug_line_end ) }\n"
		"\t.debug_frame    0 : { *(.debug_frame) }\n"
		"\t.debug_str      0 : { *(.debug_str) }\n"
		"\t.debug_loc      0 : { *(.debug_loc) }\n"
		"\t.debug_macinfo  0 : { *(.debug_macinfo) }\n"
		"\n"
		"\t.debug_weaknames 0 : { *(.debug_weaknames) }\n"
		"\t.debug_funcnames 0 : { *(.debug_funcnames) }\n"
		"\t.debug_typenames 0 : { *(.debug_typenames) }\n"
		"\t.debug_varnames  0 : { *(.debug_varnames) }\n"
		"\n"
		"\t.debug_pubtypes 0 : { *(.debug_pubtypes) }\n"
		"\t.debug_ranges   0 : { *(.debug_ranges) }\n"
		"\n"
		"\t.debug_addr     0 : { *(.debug_addr) }\n"
		"\t.debug_line_str 0 : { *(.debug_line_str) }\n"
		"\t.debug_loclists 0 : { *(.debug_loclists) }\n"
		"\t.debug_macro    0 : { *(.debug_macro) }\n"
		"\t.debug_names    0 : { *(.debug_names) }\n"
		"\t.debug_rnglists 0 : { *(.debug_rnglists) }\n"
		"\t.debug_str_offsets 0 : { *(.debug_str_offsets) }\n"
		"\t.debug_sup      0 : { *(.debug_sup) }\n"
		"\n"
		"\t.gnu.attributes 0 : { KEEP (*(.gnu.attributes)) }\n"
		"\t.mdebug         0 : { KEEP (*(.mdebug)) }\n"
		"\t.mdebug.abi32   0 : { KEEP (*(.mdebug.abi32)) }\n"
		"\n"
		"\t/DISCARD/ :\n"
		"\t{\n"
		"\t\t*(*);\n"
		"\t}\n"
	);
}

static void write_segment_script(const char *dir, const struct Segment *seg)
{
	char fname[1024];
	FILE *f;

	snprintf(fname, sizeof(fname), "%s/%s.ld", dir, seg->name);
	f = fopen(fname, "w");
	if (f == NULL)
	{
		util_fatal_error("failed to open file '%s' for writing", fname);
	}

	/* start */
	fprintf
	(
		f,
		"OUTPUT_ARCH (mips)\n"
		"\n"
		"SECTIONS\n"
		"{\n"
	);

	/* everything goes in the text section */
	fprintf(f, "\t.text :\n");
	fprintf(f, "\t{\n");
	fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);

	/* .text */
	write_text_sections(f, seg);
	fprintf(f, "\n");

	/* .data */
	write_data_sections(f, seg);
	fprintf(f, "\n");

	/* .rodata */
	write_rodata_sections(f, seg);
	fprintf(f, "\n");

	/* .sdata */
	write_sdata_sections(f, seg);
	fprintf(f, "\n");

	/* .ovl */
	write_ovl_sections(f, seg);
	fprintf(f, "\n");

	if (seg->fields & (1 << STMT_increment))
	{
		fprintf(f, "\n" "\t. += 0x%08X;\n", seg->increment);
	}

	/* .bss */
	write_bss_sections(f, seg);

	/* end */
	fprintf
	(
		f,
		"\t}\n"
		"\n"
		"\t_%sSegmentStart = ADDR(.text);\n"
		"\t_%sSegmentEnd = .;\n"
		"\t_%sSegmentSize ="
		" ABSOLUTE(_%sSegmentEnd - _%sSegmentStart);\n"
		"\n",
		seg->name,
		seg->name,
		seg->name,
		seg->name, seg->name
	);
	write_sections_finale(f);
	fprintf(f, "}\n");

	fclose(f);
}

static int segment_in_main(const struct Segment *seg)
{
	return	strcmp(seg->name, "makerom") == 0
		|| strcmp(seg->name, "boot") == 0
		|| strcmp(seg->name, "dmadata") == 0
		|| strcmp(seg->name, "code") == 0
		|| strcmp(seg->name, "buffers") == 0
		;
}

static void write_exec_script(	const char *dir,
				const struct Segment *segments,
				int segment_count)
{
	int i;

	char fname[1024];
	FILE *f;

	snprintf(fname, sizeof(fname), "%s/zelda64.ld", dir);
	f = fopen(fname, "w");
	if (f == NULL)
	{
		util_fatal_error("failed to open file '%s' for writing", fname);
	}

	/* start */
	fprintf
	(
		f,
		"OUTPUT_ARCH (mips)\n"
		"\n"
		"SECTIONS\n"
		"{\n"
	);

	for (i = 0; i < segment_count; i++)
	{
		const struct Segment *seg = &segments[i];

		if (!segment_in_main(seg))
		{
			continue;
		}

		/* content sections */
		fprintf(f, "\t..%s ", seg->name);
		if (seg->fields & (1 << STMT_after))
			fprintf(f, "(_%sSegmentEnd + %i) & ~ %i ", seg->after, seg->align - 1, seg->align - 1);
		else if (seg->fields & (1 << STMT_number))
			fprintf(f, "0x%02X000000 ", seg->number);
		else if (seg->fields & (1 << STMT_address))
			fprintf(f, "0x%08X ", seg->address);
		else
			fprintf(f, "ALIGN(0x%X) ", seg->align);
		fprintf(f, ":\n" "\t{\n");
		fprintf(f, "\t\t. = ALIGN(0x%X);\n", SUBALIGN);
		write_text_sections(f, seg);
		fprintf(f, "\n");
		write_data_sections(f, seg);
		fprintf(f, "\n");
		write_rodata_sections(f, seg);
		fprintf(f, "\n");
		write_sdata_sections(f, seg);
		fprintf(f, "\n");
		write_ovl_sections(f, seg);
		if (seg->fields & (1 << STMT_increment))
		{
			fprintf(f, "\n" "\t. += 0x%08X;\n", seg->increment);
		}
		fprintf(f, "\t}\n\n");

		/* .bss sections */
		fprintf
		(
			f,
			"\t..%s.bss (NOLOAD) :\n"
			"\t{\n",
			seg->name
		);
		write_bss_sections(f, seg);
		fprintf(f, "\t}\n");

		fprintf
		(
			f,
			"\t_%sSegmentStart = ADDR(..%s);\n"
			"\t_%sSegmentEnd = .;\n"
			"\t_%sSegmentSize ="
			" ABSOLUTE(_%sSegmentEnd - _%sSegmentStart);\n"
			"\n",
			seg->name, seg->name,
			seg->name,
			seg->name,
			seg->name, seg->name
		);
	}

	/* end */
	write_sections_finale(f);
	fprintf(f, "}\n");

	fclose(f);
}

static void write_segment_deps(const char *dir, const struct Segment *seg)
{
	int i;

	char fname[1024];
	FILE *f;

	snprintf(fname, sizeof(fname), "%s/%s.d", dir, seg->name);
	f = fopen(fname, "w");
	if (f == NULL)
	{
		util_fatal_error("failed to open file '%s' for writing", fname);
	}

	for (i = 0; i < seg->includesCount; i++)
	{
		fprintf
		(
			f,
			"%s/%s.o: %s\n",
			dir, seg->name, seg->includes[i].fpath
		);
	}

	fclose(f);
}

static void write_exec_deps(	const char *dir,
				const struct Segment *segments,
				int segment_count)
{
	int i;
	int j;

	char fname[1024];
	FILE *f;

	snprintf(fname, sizeof(fname), "%s/zelda64.d", dir);
	f = fopen(fname, "w");
	if (f == NULL)
	{
		util_fatal_error("failed to open file '%s' for writing", fname);
	}

	for (i = 0; i < segment_count; i++)
	{
		const struct Segment *seg = &segments[i];

		if (!segment_in_main(seg))
		{
			continue;
		}

		for (j = 0; j < seg->includesCount; j++)
		{
			fprintf
			(
				f,
				"%s/zelda64.elf: %s\n",
				dir, seg->includes[j].fpath
			);
		}
	}

	fclose(f);
}

static void usage(const char *execname)
{
	fprintf
	(
		stderr,
		"Ocarina of Time debugging objects linker script generator\n"
		"usage: %s <spec-file> <output-dir> [segment-name]\n",
		execname
	);
}

int main(int argc, char **argv)
{
	void *spec;
	size_t size;

	if (argc == 3)
	{
		struct Segment *segments = NULL;
		int segment_count = 0;

		spec = util_read_whole_file(argv[1], &size);
		parse_rom_spec(spec, &segments, &segment_count);

		write_exec_script(argv[2], segments, segment_count);
		write_exec_deps(argv[2], segments, segment_count);

		free_rom_spec(segments, segment_count);
		free(spec);

		return EXIT_SUCCESS;
	}
	else if (argc == 4)
	{
		struct Segment seg;

		spec = util_read_whole_file(argv[1], &size);
		if (!get_single_segment_by_name(&seg, spec, argv[3]))
		{
			util_fatal_error
			(
				"failed to find segment '%s' in '%s'",
				argv[3], argv[1]
			);
		}

		write_segment_script(argv[2], &seg);
		write_segment_deps(argv[2], &seg);

		free_single_segment_elements(&seg);
		free(spec);

		return EXIT_SUCCESS;
	}
	else
	{
		usage(argv[0]);
		return EXIT_FAILURE;
	}
}
