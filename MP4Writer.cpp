#include "MP4Writer.h"

#include <arpa/inet.h>
#include "gpac/isomedia.h"
#include "gpac/constants.h"

extern "C" {
#include "gpac/internal/media_dev.h"
}


#define INIT_STATUS    0
#define CONFIG_STATUS  1
#define CONFIG_FINISH  2


static s8 GetSampleRateID(u32 SamplRate)
{
    switch (SamplRate)
    {
         case 96000: return  0;
         case 88200: return  1;
         case 64000: return  2;
         case 48000: return  3;
         case 44100: return  4;
         case 32000: return  5;
         case 24000: return  6;
         case 22050: return  7;
         case 16000: return  8;
         case 12000: return  9;
         case 11025: return 10;
         case 8000 : return 11;
         case 7350 : return 12;
         default:    return -1;
    }
}


//gf_m4a_get_profile
static u8 GetAACProfile(u8 AudioType, u32 SampleRate, u8 Channel)
{
    switch (AudioType)
    {
        case 2: /* AAC LC */
        {
            if (Channel <= 2)  return (SampleRate <= 24000) ? 0x28 : 0x29; /* LC@L1 or LC@L2 */
            if (Channel <= 5)  return (SampleRate <= 48000) ? 0x2A : 0x2B; /* LC@L4 or LC@L5 */
                               return (SampleRate <= 48000) ? 0x50 : 0x51; /* LC@L4 or LC@L5 */
        }
        case 5: /* HE-AAC - SBR */
        {
            if (Channel <= 2)  return (SampleRate <= 24000) ? 0x2C : 0x2D; /* HE@L2 or HE@L3 */
            if (Channel <= 5)  return (SampleRate <= 48000) ? 0x2E : 0x2F; /* HE@L4 or HE@L5 */
                               return (SampleRate <= 48000) ? 0x52 : 0x53; /* HE@L6 or HE@L7 */
        }
        case 29: /*HE-AACv2 - SBR+PS*/
        {
            if (Channel <= 2)  return (SampleRate <= 24000) ? 0x30 : 0x31; /* HE-AACv2@L2 or HE-AACv2@L3 */
            if (Channel <= 5)  return (SampleRate <= 48000) ? 0x32 : 0x33; /* HE-AACv2@L4 or HE-AACv2@L5 */
                               return (SampleRate <= 48000) ? 0x54 : 0x55; /* HE-AACv2@L6 or HE-AACv2@L7 */
        }
        default: /* default to HQ */
        {
            if (Channel <= 2)  return (SampleRate <  24000) ? 0x0E : 0x0F; /* HQ@L1 or HQ@L2 */
                               return 0x10; /* HQ@L3 */
        }
    }
}

static void GetAudioSpecificConfig(u8 AudioType, u8 SampleRateID, u8 Channel, u8 *pHigh, u8 *pLow)
{
    u16 Config;

    Config = (AudioType & 0x1f);
    Config <<= 4;
    Config |= SampleRateID & 0x0f;
    Config <<= 4;
    Config |= Channel & 0x0f;
    Config <<= 3;

    *pLow  = Config & 0xff;
    Config >>= 8;
    *pHigh = Config & 0xff;
}

/* 返回的数据包括起始的4个字节0x00000001 */
static u8* FindNalu(u8 *pStart, u32 Size, u8 *pNaluType, u32 *pNaluSize)
{
    u8 *pEnd;
    u8 *pCur;
    u8 *pOut;
    u8  NaluType;

    if (4 >= Size)
        return NULL;

    /* 找第一个0x00000001 */
    pCur = pStart;
    pEnd = pStart + Size - 4;
    while (pCur < pEnd)
    {
        if ( (0 == pCur[0]) && (0 == pCur[1]) && (0 == pCur[2]) && (1 == pCur[3]) )
            break;
        pCur++;
    }
    if (pCur >= pEnd)
        return NULL;

    NaluType = (pCur[4] >> 1) & 0x3f;
    *pNaluType = NaluType;

    if (1 == NaluType || 19 == NaluType) /* P帧、I帧, 假设每一包里P帧I帧都是最后一个 */
    {
        *pNaluSize  = Size - (pCur - pStart);
        return pCur;
    }

    pOut = pCur;

    /* 找第二个0x00000001 */
    pCur += 5;
    while (pCur <= pEnd)
    {
        if ( (0 == pCur[0]) && (0 == pCur[1]) && (0 == pCur[2]) && (1 == pCur[3]) )
            break;
        pCur++;
    }
    if (pCur <= pEnd)
    {
        *pNaluSize  = pCur - pOut;
        return pOut;
    }

    *pNaluSize  = Size - (pOut - pStart);
    return pOut;
}






class MP4Writer
{
public:
    MP4Writer();
    ~MP4Writer();

    s32  CreatFile(char *strFileName);
    s32  Init265(u32 TimeScale);
    s32  Write265Sample(u8 *pData, u32 Size, u64 TimeStamp);
    s32  InitAAC(u8 AudioType, u32 SampleRate, u8 Channel, u32 TimeScale);
    s32  WriteAACSample(u8 *pData, u32 Size, u64 TimeStamp);
    void CloseFile();

private:
    GF_ISOFile *m_ptFile;

    u32 m_265TrackIndex;
    u32 m_265StreamIndex;
    u8  m_Video265Statue;
    s64 m_VideoTimeStampStart;

    GF_HEVCConfig     *m_ptHEVCConfig;
    GF_HEVCParamArray  m_tHEVCNaluParam_VPS;
    GF_HEVCParamArray  m_tHEVCNaluParam_SPS;
    GF_HEVCParamArray  m_tHEVCNaluParam_PPS;
    GF_AVCConfigSlot   m_tAVCConfig_VPS;
    GF_AVCConfigSlot   m_tAVCConfig_SPS;
    GF_AVCConfigSlot   m_tAVCConfig_PPS;

    u32 m_AACTrackIndex;
    u32 m_AACStreamIndex;
    u8  m_AudioAACStatue;
    s64 m_AudioTimeStampStart;


    void FreeAllMem();
};


MP4Writer::MP4Writer()
{
    m_ptFile = NULL;
    m_ptHEVCConfig = NULL;

    m_Video265Statue = INIT_STATUS;
    m_AudioAACStatue = INIT_STATUS;
}

MP4Writer::~MP4Writer()
{
    CloseFile();
}

s32 MP4Writer::CreatFile(char *strFileName)
{
    if (NULL != m_ptFile)
    {
        return -1;
    }

    m_ptFile = gf_isom_open(strFileName, GF_ISOM_OPEN_WRITE, NULL);
    if (NULL == m_ptFile)
    {
        return -1;
    }

    gf_isom_set_brand_info(m_ptFile, GF_ISOM_BRAND_MP42, 0);

    return 0;
}


void MP4Writer::CloseFile()
{
    if (m_ptFile)
    {
        gf_isom_close(m_ptFile);
        m_ptFile = NULL;

        FreeAllMem();
    }
}

void MP4Writer::FreeAllMem()
{
    if (m_tAVCConfig_VPS.data)
    {
        gf_free(m_tAVCConfig_VPS.data);
        m_tAVCConfig_VPS.data = NULL;
    }
    if (m_tAVCConfig_VPS.data)
    {
        gf_free(m_tAVCConfig_VPS.data);
        m_tAVCConfig_VPS.data = NULL;
    }
    if (m_tAVCConfig_VPS.data)
    {
        gf_free(m_tAVCConfig_VPS.data);
        m_tAVCConfig_VPS.data = NULL;
    }

    gf_list_del(m_tHEVCNaluParam_VPS.nalus);
    m_tHEVCNaluParam_VPS.nalus = NULL;
    gf_list_del(m_tHEVCNaluParam_SPS.nalus);
    m_tHEVCNaluParam_SPS.nalus = NULL;
    gf_list_del(m_tHEVCNaluParam_PPS.nalus);
    m_tHEVCNaluParam_PPS.nalus = NULL;

    if (m_ptHEVCConfig)
    {
        gf_list_del(m_ptHEVCConfig->param_array);
        gf_free(m_ptHEVCConfig);
        m_ptHEVCConfig = NULL;
    }

    m_Video265Statue = INIT_STATUS;
    m_AudioAACStatue = INIT_STATUS;
}

s32 MP4Writer::Init265(u32 TimeScale)
{
    if (NULL == m_ptFile || INIT_STATUS != m_Video265Statue)
        return -1;


    m_VideoTimeStampStart = -1;

    /* 创建Track */
    m_265TrackIndex = gf_isom_new_track(m_ptFile, 0, GF_ISOM_MEDIA_VISUAL, TimeScale);
    if (0 == m_265TrackIndex)
        return -1;
    if (GF_OK != gf_isom_set_track_enabled(m_ptFile, m_265TrackIndex, 1))
        return -1;


    /* 创建流 */
    m_ptHEVCConfig = gf_odf_hevc_cfg_new();
    if (NULL == m_ptHEVCConfig)
        return -1;
    m_ptHEVCConfig->nal_unit_size = 4;
    m_ptHEVCConfig->configurationVersion = 1;

    if (GF_OK != gf_isom_hevc_config_new(m_ptFile, m_265TrackIndex, m_ptHEVCConfig, NULL, NULL, &m_265StreamIndex))
        return -1;



    /* 初始化流的配置结构 */
    memset(&m_tHEVCNaluParam_VPS, 0, sizeof(m_tHEVCNaluParam_VPS));
    memset(&m_tHEVCNaluParam_SPS, 0, sizeof(m_tHEVCNaluParam_SPS));
    memset(&m_tHEVCNaluParam_PPS, 0, sizeof(m_tHEVCNaluParam_PPS));
    memset(&m_tAVCConfig_VPS,     0, sizeof(m_tAVCConfig_VPS));
    memset(&m_tAVCConfig_SPS,     0, sizeof(m_tAVCConfig_SPS));
    memset(&m_tAVCConfig_PPS,     0, sizeof(m_tAVCConfig_PPS));

    if (GF_OK != gf_list_add(m_ptHEVCConfig->param_array, &m_tHEVCNaluParam_VPS))
        return -1;
    if (GF_OK != gf_list_add(m_ptHEVCConfig->param_array, &m_tHEVCNaluParam_SPS))
        return -1;
    if (GF_OK != gf_list_add(m_ptHEVCConfig->param_array, &m_tHEVCNaluParam_PPS))
        return -1;

    m_tHEVCNaluParam_VPS.nalus = gf_list_new();
    if (NULL == m_tHEVCNaluParam_VPS.nalus)
        return -1;
    m_tHEVCNaluParam_SPS.nalus = gf_list_new();
    if (NULL == m_tHEVCNaluParam_SPS.nalus)
        return -1;
    m_tHEVCNaluParam_PPS.nalus = gf_list_new();
    if (NULL == m_tHEVCNaluParam_PPS.nalus)
        return -1;
    m_tHEVCNaluParam_VPS.type  = GF_HEVC_NALU_VID_PARAM;
    m_tHEVCNaluParam_SPS.type  = GF_HEVC_NALU_SEQ_PARAM;
    m_tHEVCNaluParam_PPS.type  = GF_HEVC_NALU_PIC_PARAM;
    m_tHEVCNaluParam_VPS.array_completeness = 1;
    m_tHEVCNaluParam_SPS.array_completeness = 1;
    m_tHEVCNaluParam_PPS.array_completeness = 1;

    if (GF_OK != gf_list_add(m_tHEVCNaluParam_VPS.nalus, &m_tAVCConfig_VPS))
        return -1;
    if (GF_OK != gf_list_add(m_tHEVCNaluParam_SPS.nalus, &m_tAVCConfig_SPS))
        return -1;
    if (GF_OK != gf_list_add(m_tHEVCNaluParam_PPS.nalus, &m_tAVCConfig_PPS))
        return -1;

    m_Video265Statue = CONFIG_STATUS;

    return 0;
}


s32 MP4Writer::Write265Sample(u8 *pData, u32 Size, u64 TimeStamp)
{
    u8  *pStart = pData;
    u8   NaluType;
    u32  NaluSize = 0;
    s32  ID;

    GF_ISOSample  tISOSample;
    HEVCState    *ptHEVCState = NULL;


    /* 265还未初始化 */
    if (INIT_STATUS == m_Video265Statue)
        return -1;


    /* 265的流还没有配置完 */
    if (CONFIG_STATUS == m_Video265Statue)
    {
        ptHEVCState = (HEVCState *)gf_malloc(sizeof(HEVCState));
        if (NULL == ptHEVCState)
            return -1;
        memset(ptHEVCState, 0, sizeof(HEVCState));
    }

    while (1)
    {
        pData = FindNalu(pData + NaluSize, Size - (u32)(pData - pStart) - NaluSize, &NaluType, &NaluSize);
        if (NULL == pData)
            break;

        /* 配置完成后只处理IP帧 */
        if (CONFIG_FINISH == m_Video265Statue)
        {
            if (1 != NaluType && 19 != NaluType) /* P帧 I帧 */
                continue;

            if (-1 == m_VideoTimeStampStart)
                m_VideoTimeStampStart = TimeStamp;

            *((u32 *)pData) = htonl(NaluSize - 4); /* 这里的长度不能包括前四个字节的头！ */
            tISOSample.data = (char *)pData;
            tISOSample.dataLength = NaluSize;
            tISOSample.IsRAP = (19 == NaluType)? RAP: RAP_NO;
            tISOSample.DTS = TimeStamp - m_VideoTimeStampStart;
            tISOSample.CTS_Offset = 0;
            if (GF_OK != gf_isom_add_sample(m_ptFile, m_265TrackIndex, m_265StreamIndex, &tISOSample))
            {
                if (ptHEVCState)
                    gf_free(ptHEVCState);
                return -1;
            }
        }
        /* 配置未完成时只处理vps sps pps头 */
        else if (CONFIG_STATUS == m_Video265Statue)
        {
            pData += 4;
            NaluSize -= 4;

            if (32 == NaluType && NULL == m_tAVCConfig_VPS.data) /* VPS */
            {
                ID = gf_media_hevc_read_vps((char *)pData , NaluSize, ptHEVCState);
                m_ptHEVCConfig->avgFrameRate      = ptHEVCState->vps[ID].rates[0].avg_pic_rate;
                m_ptHEVCConfig->temporalIdNested  = ptHEVCState->vps[ID].temporal_id_nesting;
                m_ptHEVCConfig->constantFrameRate = ptHEVCState->vps[ID].rates[0].constand_pic_rate_idc;
                m_ptHEVCConfig->numTemporalLayers = ptHEVCState->vps[ID].max_sub_layers;
                m_tAVCConfig_VPS.id   = ID;
                m_tAVCConfig_VPS.size = (u16)NaluSize;
                m_tAVCConfig_VPS.data = (char *)gf_malloc(NaluSize);
                if (NULL == m_tAVCConfig_VPS.data)
                    continue;
                memcpy(m_tAVCConfig_VPS.data, pData, NaluSize);
            }
            else if (33 == NaluType && NULL == m_tAVCConfig_SPS.data) /* SPS */
            {
                ID = gf_media_hevc_read_sps((char *)pData, NaluSize, ptHEVCState);
                m_ptHEVCConfig->tier_flag     = ptHEVCState->sps[ID].ptl.tier_flag;
                m_ptHEVCConfig->profile_idc   = ptHEVCState->sps[ID].ptl.profile_idc;
                m_ptHEVCConfig->profile_space = ptHEVCState->sps[ID].ptl.profile_space;
                m_tAVCConfig_SPS.id   = ID;
                m_tAVCConfig_SPS.size = (u16)NaluSize;
                m_tAVCConfig_SPS.data = (char *)gf_malloc(NaluSize);
                if (NULL == m_tAVCConfig_SPS.data)
                    continue;
                memcpy(m_tAVCConfig_SPS.data, pData, NaluSize);

                gf_isom_set_visual_info(m_ptFile, m_265TrackIndex, m_265StreamIndex, ptHEVCState->sps[ID].width, ptHEVCState->sps[ID].height);
            }
            else if (34 == NaluType && NULL == m_tAVCConfig_PPS.data) /* PPS */
            {
                m_tAVCConfig_PPS.id   = ID;
                m_tAVCConfig_PPS.size = (u16)NaluSize;
                m_tAVCConfig_PPS.data = (char *)gf_malloc(NaluSize);
                if (NULL == m_tAVCConfig_PPS.data)
                    continue;
                memcpy(m_tAVCConfig_PPS.data, pData, NaluSize);
            }
            else
            {
                continue;
            }

            if (m_tAVCConfig_VPS.data && m_tAVCConfig_SPS.data && m_tAVCConfig_PPS.data)
            {
                gf_isom_hevc_config_update(m_ptFile, m_265TrackIndex, m_265StreamIndex, m_ptHEVCConfig);
                m_Video265Statue = CONFIG_FINISH;
            }
        }
    }


    if (ptHEVCState)
        gf_free(ptHEVCState);

    return 0;
}


s32 MP4Writer::InitAAC(u8 AudioType, u32 SampleRate, u8 Channel, u32 TimeScale)
{
    GF_ESD *ptStreamDesc;
    s8      SampleRateID;
    u16     AudioConfig = 0;
    u8      AACProfile;
    s32     res = 0;


    if (NULL == m_ptFile || INIT_STATUS != m_AudioAACStatue)
        return -1;


    m_AudioTimeStampStart = -1;

    /* 创建Track */
    m_AACTrackIndex = gf_isom_new_track(m_ptFile, 0, GF_ISOM_MEDIA_AUDIO, TimeScale);
    if (0 == m_AACTrackIndex)
        return -1;

    if (GF_OK != gf_isom_set_track_enabled(m_ptFile, m_AACTrackIndex, 1))
        return -1;


    /* 创建并配置流 */
    SampleRateID = GetSampleRateID(SampleRate);
    if (0 > SampleRateID)
        return -1;
    GetAudioSpecificConfig(AudioType, (u8)SampleRateID, Channel, (u8*)(&AudioConfig), ((u8*)(&AudioConfig))+1);

    ptStreamDesc = gf_odf_desc_esd_new(SLPredef_MP4);
    ptStreamDesc->slConfig->timestampResolution = TimeScale;
    ptStreamDesc->decoderConfig->streamType = GF_STREAM_AUDIO;
    ptStreamDesc->decoderConfig->bufferSizeDB = 20;
    ptStreamDesc->decoderConfig->objectTypeIndication = GPAC_OTI_AUDIO_AAC_MPEG4;
    ptStreamDesc->decoderConfig->decoderSpecificInfo->dataLength = 2;
    ptStreamDesc->decoderConfig->decoderSpecificInfo->data = (char *)&AudioConfig;
    ptStreamDesc->ESID = gf_isom_get_track_id(m_ptFile, m_AACTrackIndex);
    if (GF_OK != gf_isom_new_mpeg4_description(m_ptFile, m_AACTrackIndex, ptStreamDesc, NULL, NULL, &m_AACStreamIndex))
    {
        res = -1;
        goto ERR;
    }

    if (gf_isom_set_audio_info(m_ptFile, m_AACTrackIndex, m_AACStreamIndex, SampleRate, Channel, 16))
    {
        res = -1;
        goto ERR;
    }

    AACProfile = GetAACProfile(AudioType, SampleRate, Channel);
    gf_isom_set_pl_indication(m_ptFile, GF_ISOM_PL_AUDIO, AACProfile);

    m_AudioAACStatue = CONFIG_FINISH;


    ERR:
    ptStreamDesc->decoderConfig->decoderSpecificInfo->data = NULL;
    gf_odf_desc_del((GF_Descriptor *)ptStreamDesc);

    return res;
}


s32 MP4Writer::WriteAACSample(u8 *pData, u32 Size, u64 TimeStamp)
{
    GF_ISOSample tISOSample;

    if (CONFIG_FINISH != m_AudioAACStatue)
        return -1;

    if (-1 == m_AudioTimeStampStart)
        m_AudioTimeStampStart = TimeStamp;

    tISOSample.IsRAP = RAP;
    tISOSample.dataLength = Size;
    tISOSample.data = (char *)pData;
    tISOSample.DTS = TimeStamp - m_AudioTimeStampStart;
    tISOSample.CTS_Offset = 0;
    if (GF_OK != gf_isom_add_sample(m_ptFile, m_AACTrackIndex, m_AACStreamIndex, &tISOSample))
        return -1;

    return 0;
}




extern "C" {


void* MP4_Init()
{
    return (void *)(new MP4Writer());
}

s32 MP4_CreatFile(void *pCMP4Writer, char *strFileName)
{
    if (NULL == pCMP4Writer)
        return -1;

    return ((MP4Writer *)pCMP4Writer)->CreatFile(strFileName);
}

s32 MP4_InitVideo265(void *pCMP4Writer, u32 TimeScale)
{
    if (NULL == pCMP4Writer)
        return -1;

    return ((MP4Writer *)pCMP4Writer)->Init265(TimeScale);
}

s32 MP4_Write265Sample(void *pCMP4Writer, u8 *pData, u32 Size, u64 TimeStamp)
{
    if (NULL == pCMP4Writer)
        return -1;

    return ((MP4Writer *)pCMP4Writer)->Write265Sample(pData, Size, TimeStamp);
}

s32 MP4_InitAudioAAC(void *pCMP4Writer, u8 AudioType, u32 SampleRate, u8 Channel, u32 TimeScale)
{
    if (NULL == pCMP4Writer)
        return -1;

    return ((MP4Writer *)pCMP4Writer)->InitAAC(AudioType, SampleRate, Channel, TimeScale);
}

s32 MP4_WriteAACSample(void *pCMP4Writer, u8 *pData, u32 Size, u64 TimeStamp)
{
    if (NULL == pCMP4Writer)
        return -1;

    return ((MP4Writer *)pCMP4Writer)->WriteAACSample(pData, Size, TimeStamp);
}

void MP4_CloseFile(void *pCMP4Writer)
{
    if (NULL == pCMP4Writer)
        return;

    ((MP4Writer *)pCMP4Writer)->CloseFile();
}

void MP4_Exit(void *pCMP4Writer)
{
    if (NULL == pCMP4Writer)
        return;

    delete pCMP4Writer;
}


}














