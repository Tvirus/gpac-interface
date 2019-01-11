#ifndef _MP4WRITER_H_
#define _MP4WRITER_H_

#include "gpac/setup.h"



#define MP4_AUDIO_TYPE_INVALID      0
#define MP4_AUDIO_TYPE_AAC_MAIN     1
#define MP4_AUDIO_TYPE_AAC_LC       2
#define MP4_AUDIO_TYPE_AAC_SSR      3
#define MP4_AUDIO_TYPE_AAC_LD      23




#ifdef __cplusplus
extern "C" {
#endif

void* MP4_Init();
s32  MP4_CreatFile(void *pCMP4Writer, char *strFileName);
s32  MP4_InitVideo265(void *pCMP4Writer, u32 TimeScale);
s32  MP4_Write265Sample(void *pCMP4Writer, u8 *pData, u32 Size, u64 TimeStamp);
s32  MP4_InitAudioAAC(void *pCMP4Writer, u8 AudioType, u32 SampleRate, u8 Channel, u32 TimeScale);
s32  MP4_WriteAACSample(void *pCMP4Writer, u8 *pData, u32 Size, u64 TimeStamp);
void MP4_CloseFile(void *pCMP4Writer);
void MP4_Exit(void *pCMP4Writer);


#ifdef __cplusplus
}
#endif



#endif