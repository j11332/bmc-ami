#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <malloc.h>
#include <libgen.h>
#include <errno.h>

#include "fmh.h"

static uint8_t FirmwareInfo[64 * 1024];
static uint32_t Location;	/* Flash Location Value */
static size_t BlockSize;	/* Size of each Flash Block */
extern char *optarg;
static int summary = 0;

static void update_name(MODULE_INFO *mod, char *name) {
	char *p = name;

	strncpy(name, (char *)mod->name, 8);
	name[8] = '\0';

	while (*p) {
		*p = toupper(*p);
		p++;
	}
}

static void dump_fmh(FMH *fmh, MODULE_INFO *mod, char *name, FILE *out) {
	int is_mod = 1;
	int comp = 0;

//	printf("*** %s ***\n", name);

	if (summary) {
		fprintf(out, "%-16s\t%d.%d\t0x%04x\n",
			name, mod->ver_Major, mod->ver_Minor, mod->type);
		return;
	}

	fprintf(out, "[%s]\n", name);

	if (mod->type == MODULE_FMH_FIRMWARE
	    || mod->type == MODULE_FIRMWARE_1_4) {
		is_mod = 0;
	}

	/* Version output */
	fprintf(out, "\tMajor\t\t= %d\n\tMinor\t\t= %d\n",
		mod->ver_Major, mod->ver_Minor);

	/* Type output */
	fprintf(out, "\tType\t\t= 0x%04x\n", mod->type);


	if (is_mod) {
		/* Alloc output */
		if (fmh->AllocatedSize - mod->Location > mod->Size + BlockSize)
			fprintf(out, "\tAlloc\t\t= %dK\n",
				fmh->AllocatedSize / 1024);

		/* Output flags */
		fprintf(out, "\tCheckSum\t= %s\n",
			(mod->Flags & MODULE_FLAG_VALID_CHECKSUM) ? "YES" : "NO");

		if (mod->Flags & MODULE_FLAG_BOOTPATH_OS)
			fprintf(out, "\tBootOS\t\t= YES\n");

		if (mod->Flags & MODULE_FLAG_BOOTPATH_DIAG)
			fprintf(out, "\tBootDIAG\t= YES\n");

		if (mod->Flags & MODULE_FLAG_BOOTPATH_RECOVERY)
			fprintf(out, "\tBootRECO\t= YES\n");

		if (mod->Flags & MODULE_FLAG_EXECUTE)
			fprintf(out, "\tExecute\t\t= YES\n");

		if (mod->Flags & MODULE_FLAG_COPY_TO_RAM)
			fprintf(out, "\tCopyToRAM\t= YES\n");

		comp = (mod->Flags & MODULE_FLAG_COMPRESSION_MASK) >> MODULE_FLAG_COMPRESSION_LSHIFT;
		if (comp > 0)
			fprintf(out, "\tCompress\t= %s\n", comp);

		/* Filename output */
		fprintf(out, "\tFile\t\t= %s.bin\n", name);
	}

	/* Locate output */
	if (fmh->Location < BlockSize)
		fprintf(out, "\tLocate\t\t= \"START\"\n");
	else if (fmh->Location == Location)
		fprintf(out, "\tLocate\t\t= \"END\"\n");
	else if (fmh->Location / 1024 < 2049)
		fprintf(out, "\tLocate\t\t= %dK\t\t;0x%x\n",
			fmh->Location / 1024, fmh->Location);
	else
		fprintf(out, "\tLocate\t\t= 0x%x\t\t;%dK\n",
			fmh->Location, fmh->Location / 1024);

	if (is_mod) {
		/* Offset output */
		if (mod->Location > 0x40)
			fprintf(out, "\tOffset\t\t= %dK\n",
				mod->Location / 1024);

		/* Load output */
		if (mod->Load_Address != 0xFFFFFFFF)
			fprintf(out, "\tLoad\t\t= %dM\n",
				mod->Load_Address / 1024 / 1024);

		/* Alternate FMH location output */
		if (fmh->Location % BlockSize != 0)
			fprintf(out, "\tFMHLoc\t\t= 0x%04x\t\t; %dK - 128\n",
				fmh->Location, (fmh->Location+128) / 1024);
	}

	fputs("\n", out);
}

static void dump_module(FMH *fmh, MODULE_INFO *mod, char *name, FILE *fd, const char *dir) {
	FILE *out;
	char outfile[256];
	char *in_p;

	if (mod->type == MODULE_FMH_FIRMWARE
	    || mod->type == MODULE_FIRMWARE_1_4)
	{
		return;
	}

#if 0
	printf("\nFMH Size %08x Location %08x Alloc %08x\n", fmh->Size, fmh->Location, fmh->AllocatedSize);
	printf("MOD Size %08x Location %08x\n", mod->Size, mod->Location);
	printf("mmap %08x @ %08x\n", fmh->AllocatedSize, fmh->Location + mod->Location);
#endif

	printf(" -- processing %s...\n", name);

	in_p = mmap(NULL, fmh->AllocatedSize, PROT_READ,
		    MAP_SHARED, fileno(fd), fmh->Location & (~0xffff));

	snprintf(outfile, 256, "%s/%s.bin", dir, name);
	out = fopen(outfile, "w+");
	if (out == NULL)
	{
		printf("Error: Unable to create file %s\n", outfile);
		return;
	}

	if (fwrite(in_p + mod->Location, mod->Size, 1, out) != 1)
	{
		printf("Error: Unable to write to file %s\n", outfile);
		return;
	}

	munmap(in_p, fmh->AllocatedSize);

	fclose(out);
}

static void dump_fwinfo(uint8_t *fwinfo, FILE *out) {
	int j;
	char *str, *token;
	char *key;
	char *value;
	char *saveptr1, *saveptr2;
	uint8_t *p;

	/* Check for FirmwareInfo data */
	p = (uint8_t *)index((char *)fwinfo, 0xff);
	if (p == NULL || p == fwinfo)
		return;

	*p = '\0';
//	printf("%s\n", fwinfo);

	for (j = 1, str = (char *)fwinfo; ; j++, str = NULL)
	{
		token = strtok_r(str, "\n", &saveptr1);
		if (token == NULL)
			break;

		key = strtok_r(token, "=", &saveptr2);
		if (key == NULL)
			continue;
		value = strtok_r(NULL, "=", &saveptr2);
		if (value == NULL)
			continue;
		printf("%s=\"%s\"\n", key, value);
		if (summary)
			continue;

		if (!strcmp(key, "FW_VERSION")) {
			int mj, mn, no;
			if (sscanf(value, "%d.%d.%d", &mj, &mn, &no) == 3)
			{
				fprintf(out, "\tBuildNo\t\t= %d\n", no);
			}
		} else if (!strcmp(key, "FW_PRODUCTID")) {
			fprintf(out, "\tProductId\t= %d\n", atoi(value));
		} else if (!strcmp(key, "FW_PRODUCTNAME")) {
			fprintf(out, "\tProductName\t= \"%s\"\n", value);
		} else if (!strncmp(key, "OEM_", 4)) {
			printf(" *** TODO: Process OEM keys here! ***\n");
		}
	}
}

static void
Usage(char *Prog, int status) {
	printf("Usage is %s <Args>\n", Prog);
	printf("Args are :\n");
	printf("\t -i Input Firmware File\n");
	printf("\t -o Output Firmware Path\n");
	printf("\t -b Block Size (in kB, default 64)\n");
	printf("\t -s Summary\n");
	printf("\t -f Offset to the FMH header\n");
	printf("\n");
	exit(status);
}

int main(int argc, char *argv[]) {
	int opt;
	long fmh_offset = 0;

	/* Global Information */	
	char *OutDir;			/* Location of Output Files */
	char *fw_file;

	/* Output File Creation Related */	
	FILE *Outfd;			/* Output File Descriptor */
	char ini_name[256];

	/* FMH Related */
	FILE *Infd;
	FMH *fmh = NULL;
	MODULE_INFO *mod = NULL;	/* Module Information */
	char ModuleName[9];

	/* RACTRENDS releted */
//	int FirmwareMajor,FirmwareMinor;
	struct stat stat;

	/* Initialize with empty values */
	OutDir = NULL;
	fw_file = NULL;
	ini_name[0] = '\0';
	BlockSize = 0;

	while ((opt = getopt(argc, argv, "i:o:b:f:hs")) != -1)
	{
		 switch (opt)
		 {
			case 'i':
				fw_file = strdup(optarg);
				break;
			case 'o':
				OutDir = strdup(optarg);
				break;
			case 'b':
				BlockSize = atoi(optarg) * 0x1000;
				break;
			case 'f':
				fmh_offset = strtol(optarg, NULL, 16);
				break;
			case 's':
				summary = 1;
				break;
			default:
				Usage("dumpimage", opt != 'h');
				break;
		}
	}

	if (fw_file == NULL || (OutDir == NULL && !summary))
		Usage("dumpimage", 2);

	if (!summary)
	{
		snprintf(ini_name, 256, "%s/genimage.ini", OutDir);

		if (mkdir(OutDir, 0755) < 0)
		{
			perror("Error: Unable to create directory");
			return 3;
		}
	}

	if (BlockSize == 0)
		BlockSize = 0x10 * 0x1000;

	Infd = fopen(fw_file, "r");
	if (Infd == NULL)
	{
		printf("Error: Unable to open firmware file %s\n", fw_file);
		return 3;
	}

	if (fstat(fileno(Infd), &stat) < 0)
	{
		printf("Error: Stat failed for firmware file %s\n", fw_file);
		return 3;
	}

	
	if (fmh_offset == 0) {
		if (fseek(Infd, -BlockSize, SEEK_END) < 0) {
			printf("Error: Seek to the end of firmware file %s failed\n", fw_file);
			return 3;
		}
	} else {

		if (fseek(Infd, fmh_offset, SEEK_SET) < 0) {
			printf("Error: Seek to the specified FMH offset in %s failed\n", fw_file);
			return 3;
		}
	}

	if (fread(FirmwareInfo, BlockSize, 1, Infd) != 1) {
		printf("Error: Read of firmware file %s failed\n", fw_file);
		return 3;
	}

	fmh = ScanforFMH((uint8_t *)FirmwareInfo, BlockSize);
	if (fmh == NULL) {
		printf("Error: Can not find FMH header in %s\n", fw_file);
		return 3;
	}

	if (!summary) {
		Outfd = fopen(ini_name, "w+");
		if (Outfd == NULL)
		{
			printf("Error: Unable to open output file %s\n", ini_name);
			return 3;
		}
	} else {
		Outfd = stdout;
	}

	mod = &(fmh->Module_Info);
	update_name(mod, ModuleName);

	Location = fmh->Location;

	if (!summary)
	{
		fprintf(Outfd, "[GLOBAL]\n\tOutput  \t= %s\n\tFlashSize \t= %dM\n\tBlockSize\t= %dK\n",
			basename(fw_file), stat.st_size / 0x100000, BlockSize / 1024);
	}

	dump_fwinfo(FirmwareInfo + FMH_SIZE, Outfd);

	if (!summary)
		fputs("\n", Outfd);
	else
		fprintf(Outfd, "--------------------------------------\n");
	dump_fmh(fmh, mod, ModuleName, Outfd);

#if 0
	FirmwareMajor = mod->ver_Major;
	FirmwareMinor = mod->ver_Minor;

	printf("FW %d.%d\n", FirmwareMajor, FirmwareMinor);
	printf("Size %08x Location %08x\n", fmh->Size, fmh->Location);
#endif
	fseek(Infd, 0, SEEK_SET);

	while (fread(FirmwareInfo, BlockSize, 1, Infd) == 1)
	{
		fmh = ScanforFMH((uint8_t *)FirmwareInfo, BlockSize);
		if (fmh == NULL)
		{
			// Possible GAP, try next block
			continue;
		}

		// Last block reached, stop
		if (fmh->Location == Location)
			break;

		mod = &(fmh->Module_Info);
		update_name(mod, ModuleName);

		dump_fmh(fmh, mod, ModuleName, Outfd);
		if (!summary)
			dump_module(fmh, mod, ModuleName, Infd, OutDir);

		fseek(Infd, fmh->AllocatedSize - BlockSize, SEEK_CUR);
	}

	fclose(Infd);

	if (!summary)
		fclose(Outfd);

	return 0;
}
