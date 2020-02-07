#ifdef __KERNEL__
	#ifdef __UBOOT__
		#include <common.h>
	#else
		#include <linux/kernel.h>
		#include <linux/string.h>
	#endif
#else
	#include <stdio.h>
	#include <string.h>
#endif
#include "fmh.h"
#include "crc32.h"
#include <stdarg.h>

unsigned char CalculateModule100(unsigned char *Buffer, uint32_t Size);
static FMH * CheckForNormalFMH(FMH *fmh);
static uint32_t CheckForAlternateFMH(ALT_FMH *altfmh);

int gDebug = 0;
int debugPrint(const char *format, ...){
    va_list ap;
    int ret = 0;
    va_start(ap, format);
    if (gDebug) {
        ret = vfprintf(stderr, format, ap);
    }
    va_end(ap);
    return ret;
}

unsigned char CalculateModule100(unsigned char *Buffer, uint32_t Size) {
	unsigned char Sum=0;

	while (Size--)
	{
		Sum+=(*Buffer);
		Buffer++;
	}	

	return (~Sum)+1;
}

static unsigned char ValidateModule100(unsigned char *Buffer, uint32_t Size) {
	unsigned char Sum=0;

	while (Size--)
	{
		Sum+=(*Buffer);
		Buffer++;
	}

	return Sum;
}

static FMH *CheckForNormalFMH(FMH *fmh) {
	if (strncmp((char *)fmh->Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1) != 0)
			return NULL;

	if (le16_to_host(fmh->End_Signature) != FMH_END_SIGNATURE)
			return NULL;

	if (ValidateModule100((unsigned char *)fmh,sizeof(FMH)) != 0)
			return NULL;
	
	return fmh;
			
}

static uint32_t CheckForAlternateFMH(ALT_FMH *altfmh) {

	for(int i = 0; i < sizeof(ALT_FMH); i++){
		debugPrint("%02X ", *((uint8_t *)(altfmh + i)));
	}
	debugPrint("\n");

	if (strncmp((char *)altfmh->Signature, FMH_SIGNATURE, sizeof(FMH_SIGNATURE)-1) != 0){
		debugPrint("Invalid signature.\n");
		return INVALID_FMH_OFFSET;
	}

	if (le16_to_host(altfmh->End_Signature) != FMH_END_SIGNATURE){
		debugPrint("Invalid end signature.\n");
		return INVALID_FMH_OFFSET;
	}

	if (ValidateModule100((unsigned char *)altfmh,sizeof(ALT_FMH)) != 0){
		debugPrint("Invalid checksum.\n");
		return INVALID_FMH_OFFSET;
	}
	
	return le32_to_host(altfmh->FMH_Link_Address);

}

FMH *ScanforFMH(unsigned char *SectorAddr, size_t SectorSize) {
	FMH *fmh;
	ALT_FMH *altfmh;
	uint32_t FMH_Offset;
	for(int i = 0; i < sizeof(FMH); i++){
		if (i % 16 == 0){
			debugPrint("\n");
		}
		debugPrint("%02X ", *((uint8_t *)(SectorAddr + i)));
	}
	debugPrint("\n");

	/* Check if Normal FMH is found */
	fmh = (FMH *)SectorAddr;
	fmh = CheckForNormalFMH(fmh);
	if (fmh != NULL)
		return fmh;

	/* If Normal FMH is not found, check for alternate FMH */
	altfmh = (ALT_FMH *)(SectorAddr + SectorSize - sizeof(ALT_FMH));
	FMH_Offset = CheckForAlternateFMH(altfmh);
	if (FMH_Offset == INVALID_FMH_OFFSET)
		return NULL;
	fmh = (FMH *)(SectorAddr +FMH_Offset);
	
	/* If alternate FMH is found, validate it */
	fmh = CheckForNormalFMH(fmh);
	return fmh;
}

void CreateFMH(FMH *fmh,uint32_t AllocatedSize, MODULE_INFO *mod, uint32_t Location) {
	/* Clear the Structure */	
	memset((void *)fmh,0,sizeof(FMH));

	/* Copy the module information */
	memcpy((void *)&(fmh->Module_Info),(void *)mod,sizeof(MODULE_INFO));
					
	/* Fill the FMH Fields */		
	// strncpy((char *)fmh->Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1);
	memcpy(fmh->Signature, FMH_SIGNATURE, sizeof(fmh->Signature));
	fmh->FMH_Ver_Major 		= FMH_MAJOR;
	fmh->Ver_Minor 		= FMH_MINOR;
	fmh->Size	   		= FMH_SIZE;
	fmh->End_Signature	= host_to_le16(FMH_END_SIGNATURE);
	
	fmh->AllocatedSize	= host_to_le32(AllocatedSize);
	fmh-> Location = host_to_le32(Location);

	/*Calculate Header Checksum*/
	fmh->Header_Checksum = CalculateModule100((unsigned char *)fmh,sizeof(FMH));
		
	return;
}

void CreateAlternateFMH(ALT_FMH *altfmh,uint32_t FMH_Offset)  {
	/* Clear the Structure */	
	memset((void *)altfmh,0,sizeof(ALT_FMH));
					
	/* Fill the FMH Fields */		
	// strncpy((char *)altfmh->Signature,FMH_SIGNATURE,sizeof(FMH_SIGNATURE)-1);
	memcpy(altfmh->Signature, FMH_SIGNATURE, sizeof(altfmh->Signature));
	altfmh->End_Signature = host_to_le16(FMH_END_SIGNATURE);
	
	altfmh->FMH_Link_Address = host_to_le32(FMH_Offset);

	/*Calculate Header Checksum*/
	altfmh->Header_Checksum = CalculateModule100((unsigned char *)altfmh,
										sizeof(ALT_FMH));
	return;
}

uint32_t CalculateCRC32(unsigned char *Buffer, size_t Size) {
    uint32_t crc32 = 0xFFFFFFFF;
	int i;

	/* Read the data and calculate crc32 */	
    for(i = 0; i < Size; i++)
		crc32 = ((crc32) >> 8) ^ CrcLookUpTable[(Buffer[i]) 
								^ ((crc32) & 0x000000FF)];
	return ~crc32;
}

void BeginCRC32(uint32_t *crc32) {
	*crc32 = 0xFFFFFFFF;
	return;
}

void DoCRC32(uint32_t *crc32, unsigned char Data) {
	*crc32=((*crc32) >> 8) ^ CrcLookUpTable[Data ^ ((*crc32) & 0x000000FF)];
	return;
}

void EndCRC32(uint32_t *crc32) {
	*crc32 = ~(*crc32);
	return;
}
