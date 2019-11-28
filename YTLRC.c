/*
    YottaChain Repairable Code
	Copyright (c) 2019 YottaChain Foundation Ltd.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of CM256 nor the names of its contributors may be
	  used to endorse or promote products derived from this software without
	  specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
	LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
	CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
	CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
	POSSIBILITY OF SUCH DAMAGE.
*/

//#include <stdlib.h>
#include "cm256.h"
#include "ytlrc.h"

typedef struct {    
    bool bIsUsed;
    CM256LRC param;
    uint8_t *pDecodedData;

    unsigned short numShards;
    CM256Block shards[MAXSHARDS];
    short horMissed[MAXSHARDS / MAXHORCOUNT];   // number of missed shards in each horizonal local groups of original data, ignore missed recovery shard(s)
    short verMissed[MAXHORCOUNT];   // number of missed shards in each vertical local groups of original data, ignore missed recovery shard(s)
    short globalMissed;   // number of missed shards of original data, ignore missed recovery shard(s)
    short numGlobalRecovery;
    short totalGlobalRecovery; // count in global recovery shard from horizonal recovery shards and vertical recovery shards
    short numHorRecovery, numVerRecovery;

    uint8_t *pBuffer;   // Buffer for at most 3 shards: one for repair global recovery shard, one for additional global recovery shard from horizonal recovery shards, one for additional global recovery shard from vertical recovery shard
} DecoderLRC;
#define SHARD_EXISTED(pDecoder, index)   (NULL != pDecoder->shards[index].pData)
#define HOR_DECODE_INDEX(pParam)  (pParam->TotalOriginalCount)
#define VER_DECODE_INDEX(pParam)  (pParam->TotalOriginalCount+1)
#define GLOBAL_DECODE_INDEX(pParam, i)  (pParam->TotalOriginalCount+i+2)
#define GLOBAL_FROM_HOR_INDEX(pParam)   (pParam->OriginalCount + pParam->TotalRecoveryCount)
#define GLOBAL_FROM_VER_INDEX(pParam)   (pParam->OriginalCount + pParam->TotalRecoveryCount + 1)

static DecoderLRC *decoders = NULL;
static short numDecoders = 0;
static short globalRecoveryCount = 10;

static inline uint8_t *GlobalRecoveryBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer;
}

static inline uint8_t *GlobalFromHorBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer + pDecoder->param.BlockBytes;
}

static inline uint8_t *GlobalFromVerBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer + 2 * pDecoder->param.BlockBytes;
}


static uint8_t numMissing16[16] = {
    /*0000 0001 0010 0011 0100 0101 0110 0111 1000 1001 1010 1011 1100 1101 1110 1111F*/
        4,  3,   3,   2,   3,   2,   2,   1,   3,   2,   2,   1,    2,   1,   1,   0
};
static uint8_t numMissing256[256] = {
    /*00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F*/
        8, 7, 7, 6, 7, 6, 6, 5, 7, 6, 6, 5, 6, 5, 5, 4,
};


/*
 * Initialize
 * numGlobalRecoveryCount: number of global recovery shards
 * maxHandles: maximum of decoders working at same time
 * return: 0 if fails
 */
extern short InitialLRC(short n, short maxHandles)
{
    short i, j;
    if (cm256_init() || n <= 2 || maxHandles <= 0)
        return false;
    decoders = malloc(maxHandles * sizeof(DecoderLRC));
    if (NULL == decoders)
        return false;
    globalRecoveryCount = n - 2;
    numDecoders = maxHandles;
    for (i = 0; i < numDecoders; i++)
        decoders[i].bIsUsed = false;

    for (i = 0; i < 256; i++)
        numMissing256[i] = numMissing16[i >> 4] + numMissing16[i & 0xf];

    return true;
}

/*
 * Encode original data, return recovery shards
 * originalShards: shards for original data, 1st byte of each shard is its index
 * shardSize: size of each shard
 * originalCount: number of shards of original data
 * pRecoveryData: required at least MAXRECOVERYSHARDS*(shardSize+1) space, return recovery shards after encoding,
 *                length of each shard is shardSize+1, the leading byte of each shard is index of this shard
 * return: number of recovery shards, <=0 fails
 */
short EncodeLRC(const void *originalShards[], unsigned short originalCount, unsigned long shardSize, void *pRecoveryData)
{
    CM256LRC    param;
    CM256Block blocks[256];
    short i;
    uint8_t *pZeroData = NULL;

    if (NULL == originalShards || originalCount <= 0 || originalCount > 230 || shardSize <= 1 || NULL == pRecoveryData)
        return -1;
    InitialParam(&param, originalCount, shardSize, true);
    for (i = 0; i < originalCount; i++)
        blocks[i].pData = (uint8_t*)(originalShards[i] + 1);    // Ignore the index byte
    pZeroData = malloc(shardSize-1);
    if (NULL == pZeroData)
        return -2;
    memset(pZeroData, 0, shardSize-1);
    for (i = originalCount; i < param.TotalOriginalCount; i++)
        blocks[i].pData = pZeroData;

    int ret = cm256_encode(param, blocks, pRecoveryData);

    free(pZeroData);
    return ret == 0 ? param.TotalRecoveryCount : -3;
}


/*
 * Begin of new decode process
 * originalCount: number of shards of original data
 * shardSize: size of each shard in byte including index byte
 * pData: require at least originalCount * (shardSize-1) space, return original data if success
 * return: handle of this decode process, <0 fails (such as exceed maxHandles)
 */
extern short BeginDecode(unsigned short originalCount, unsigned long shardSize, void *pData)
{
    short i, j;
    if (originalCount <= 0 || originalCount >= MAXSHARDS || NULL == pData)
        return -1;
    for (i = 0; i < numDecoders; i++) {
        if ( !decoders[i].bIsUsed ) {
            DecoderLRC *pDecoder = &decoders[i];
            
            InitialParam(&pDecoder->param, originalCount, shardSize, true);

            pDecoder->bIsUsed = true;
            pDecoder->pDecodedData = pData;
            pDecoder->numShards = 0;
            for (j = 0; j < MAXSHARDS; j++)
                pDecoder->shards[j].pData = NULL;
            pDecoder->pBuffer = malloc(3 * shardSize);
            /* Do not need to check return value because this buffer is used for repariing global recovery shard, */
            /* decoding is still working even if malloc fails, but maybe lost up to 3 global recovery ability */

            pDecoder->globalMissed = pDecoder->param.OriginalCount;
            pDecoder->numGlobalRecovery = 0;
            pDecoder->totalGlobalRecovery = 0;
            pDecoder->numHorRecovery = 0;
            pDecoder->numVerRecovery = 0;
            for (j = 0; j < pDecoder->param.VerLocalCount; j++)
                pDecoder->horMissed[j] = pDecoder->param.HorLocalCount;
            pDecoder->horMissed[pDecoder->param.VerLocalCount - 1] -= pDecoder->param.TotalOriginalCount - pDecoder->param.OriginalCount;
            for (j = 0; j < pDecoder->param.HorLocalCount; j++) {
                pDecoder->verMissed[j] = pDecoder->param.VerLocalCount;
                if ( j >= (pDecoder->param.OriginalCount % pDecoder->param.HorLocalCount) )
                    pDecoder->verMissed[j]--;
            }

            return i;
        }
    }
    return -1;
}

/* Recover a horizaonal local group when possible, return the x coordinate of recovered shard, <0 means failed */
static short CheckAndRecoverHor(DecoderLRC *pDecoder, short y)
{
    short x;
    CM256LRC *pParam = &pDecoder->param;
    if ( pDecoder->horMissed[y] != 1 || !SHARD_EXISTED(pDecoder, pParam->OriginalCount + pParam->FirstHorRecoveryIndex + y) )
        return -1;
    /* Only miss one shard in this horizonal local group and the recovery shard of this group exists, recover the missing shard */
    unsigned short index2 = y * pParam->HorLocalCount;
    for (x = 0; x < pParam->HorLocalCount; x++, index2++) {
        if ( !SHARD_EXISTED(pDecoder, index2) ) {
            /* Found the missing shard, recover it */
            pDecoder->shards[index2].lrcIndex = pParam->TotalOriginalCount + pParam->FirstHorRecoveryIndex + y;
            pDecoder->shards[index2].decodeIndex = HOR_DECODE_INDEX(pParam);
            pDecoder->shards[index2].pData = pDecoder->pDecodedData + index2 * pParam->BlockBytes;
            /* Copy horizontal recovery shard data to missing shard, it will be recovered to decoded data by cm256_decode */
            memcpy(pDecoder->shards[index2].pData, pDecoder->shards[pParam->OriginalCount + pParam->FirstHorRecoveryIndex + y].pData, pParam->BlockBytes);

            cm256_encoder_params params;
            params.BlockBytes = pParam->BlockBytes;
            params.TotalOriginalCount = pParam->TotalOriginalCount;
            params.FirstElement = y * pParam->HorLocalCount;
            params.OriginalCount = pParam->HorLocalCount;
            params.RecoveryCount = 1;
            params.Step = 1;

            if (cm256_decode(params, pDecoder->shards) != 0) {
                pDecoder->shards[index2].pData = NULL;  // Decode error, it is not recovered
                return -2;
            }

            if ( pDecoder->horMissed[y] > 0)    // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->horMissed[y]--;
            if ( pDecoder->verMissed[x] > 0 )    // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->verMissed[x]--;
            if ( pDecoder->globalMissed > 0 )    // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->globalMissed--;

            return x;
        }
    }
    return -3;  // This branch is imppossible unless there is a bug
}

/* Recover a vertical local group when possible, return the y coordinate of recovered shard, <0 means failed */
static short CheckAndRecoverVer(DecoderLRC *pDecoder, short x)
{
    short y;
    CM256LRC *pParam = &pDecoder->param;
    if ( pDecoder->verMissed[x] != 1 || !SHARD_EXISTED(pDecoder, pParam->OriginalCount + pParam->FirstVerRecoveryIndex + x) )
        return -1;
    /* Only miss one shard in this vertical local group and the recovery shard of this group exists, recover the missing shard */
    unsigned short index2 = x;
    for (y = 0; y < pParam->VerLocalCount; y++) {
        if ( !SHARD_EXISTED(pDecoder, index2) ) {
            /* Found the missing shard, recover it */
            pDecoder->shards[index2].lrcIndex = pParam->TotalOriginalCount + pParam->FirstVerRecoveryIndex + x;
            pDecoder->shards[index2].decodeIndex = VER_DECODE_INDEX(pParam);
            pDecoder->shards[index2].pData = pDecoder->pDecodedData + index2 * pParam->BlockBytes;
            /* Copy horizontal recovery shard data to missing shard, it will be recovered to decoded data by cm256_decode */
            memcpy(pDecoder->shards[index2].pData, pDecoder->shards[pParam->OriginalCount + pParam->FirstVerRecoveryIndex + x].pData, pParam->BlockBytes);

            cm256_encoder_params params;
            params.BlockBytes = pParam->BlockBytes;
            params.TotalOriginalCount = pParam->TotalOriginalCount;
            params.FirstElement = x;
            params.OriginalCount = pParam->VerLocalCount;
            params.RecoveryCount = 1;
            params.Step = pParam->HorLocalCount;

            if (cm256_decode(params, pDecoder->shards) != 0) {
                pDecoder->shards[index2].pData = NULL;  // Decode error, it is not recovered
                return -2;
            }

            /* Recovery success */
            if ( pDecoder->horMissed[y] > 0)    // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->horMissed[y]--;
            if ( pDecoder->verMissed[x] > 0 )    // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->verMissed[x]--;
            if ( pDecoder->globalMissed > 0 )    // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->globalMissed--;

            return y;
        }
        index2 += pParam->HorLocalCount;
    }
    return -3;  // This branch is imppossible unless there is a bug
}

/* Recover local group of global recovery shards when possible */
static bool CheckAndRecoverGlobal(DecoderLRC *pDecoder)
{
    short i;
    bool ret = false;
    CM256LRC *pParam = &pDecoder->param;
    if ( NULL == pDecoder->pBuffer )
        return false;
    if ( pDecoder->numGlobalRecovery == pParam->GlobalRecoveryCount-1 && SHARD_EXISTED(pDecoder, pParam->OriginalCount + pParam->LocalRecoveryOfGlobalRecoveryIndex) ) {
        /* Only miss one global recovery shard, recovery it from local recovery shard of global recovery shards */
        memcpy(pDecoder->pBuffer, pDecoder->shards[pParam->OriginalCount + pParam->LocalRecoveryOfGlobalRecoveryIndex].pData, pParam->BlockBytes);
        for (i = 0; i < pParam->GlobalRecoveryCount; i++) {
            short index = pParam->OriginalCount + pParam->FirstGlobalRecoveryIndex + i;
            if ( !SHARD_EXISTED(pDecoder, index) ) {
                /* Found the missing shard, repair it */
                pDecoder->shards[index].pData = pDecoder->pBuffer; // lrcIndex and decodeIndex of this shard are not used hereinafter
            } else {
                assert(pDecoder->shards[index].pData != pDecoder->pBuffer);
                gf256_add_mem(pDecoder->pBuffer, pDecoder->shards[index].pData, pParam->BlockBytes);
            }
        }
        pDecoder->numGlobalRecovery++;
        pDecoder->totalGlobalRecovery++;
        ret = true;
    }

    CM256Block *pShard = &pDecoder->shards[GLOBAL_FROM_HOR_INDEX(pParam)];
    if ( pDecoder->numHorRecovery == pParam->VerLocalCount && NULL == pShard->pData ) {
        /* There is an additional global recovery shard from horizonal recovery shards */
        uint8_t *pBuf = GlobalFromHorBuf(pDecoder);
        memcpy(pBuf, pDecoder->shards[pParam->FirstHorRecoveryIndex].pData, pParam->BlockBytes);
        for (i = 1; i < pParam->VerLocalCount; i++)
            gf256_add_mem(pBuf, pDecoder->shards[pParam->FirstHorRecoveryIndex + i].pData, pParam->BlockBytes);
        pShard->pData = pBuf;
        pShard->lrcIndex = GLOBAL_FROM_HOR_INDEX(pParam);
        pShard->decodeIndex = GLOBAL_DECODE_INDEX(pParam, 0);

        pDecoder->totalGlobalRecovery++;
        ret = true;
    }

    pShard = &pDecoder->shards[GLOBAL_FROM_VER_INDEX(pParam)];
    if ( pDecoder->numVerRecovery == pParam->HorLocalCount && NULL == pShard->pData ) {
        /* There is an additional global recovery shard from vertical recovery shards */
        uint8_t *pBuf = GlobalFromVerBuf(pDecoder);
        memcpy(pBuf, pDecoder->shards[pParam->FirstVerRecoveryIndex].pData, pParam->BlockBytes);
        for (i = 1; i < pParam->HorLocalCount; i++)
            gf256_add_mem(pBuf, pDecoder->shards[pParam->FirstVerRecoveryIndex + i].pData, pParam->BlockBytes);
        pShard->pData = pBuf;
        pShard->lrcIndex = GLOBAL_FROM_VER_INDEX(pParam);
        pShard->decodeIndex = GLOBAL_DECODE_INDEX(pParam, 1);

        pDecoder->totalGlobalRecovery++;
        ret = true;
    }
    return ret;
}

/*
 * Decode one shard for specific decode process
 * handle: handle of decode process
 * in: data of this shard
 * return: 0 if collected shards are not enough for decoding, >0 success, automatically free handle, <0 error
 */
extern short DecodeLRC(short handle, const void *pData)
{
    short i, j;

    if (handle < 0 || handle >= numDecoders || !decoders[handle].bIsUsed || NULL == pData)
        return -1;
    DecoderLRC *pDecoder = &decoders[handle];
    uint8_t *pShard = (uint8_t *)pData;
    uint8_t index = pShard[0];
    if (index > pDecoder->param.OriginalCount + pDecoder->param.TotalRecoveryCount)
        return -2;
    if ( SHARD_EXISTED(pDecoder, index) )
        return 0;   // Already calculated or received
    
    short x, y;
    pDecoder->numShards++;
    CM256LRC *pParam = &pDecoder->param;
    if ( index < pParam->OriginalCount ) {
        /* Original data */
        pDecoder->shards[index].lrcIndex = index;
        pDecoder->shards[index].decodeIndex = index;
        pDecoder->shards[index].pData = pDecoder->pDecodedData + index * pParam->BlockBytes;
        memcpy(pDecoder->shards[index].pData, pShard + 1, pParam->BlockBytes);  // Copy to destinaltion

        y = index / pParam->HorLocalCount;
        x = index % pParam->HorLocalCount;
        if ( pDecoder->horMissed[y] > 0)    // In fact, it should be always greater than zero here unless there is a bug
            pDecoder->horMissed[y]--;
        if ( pDecoder->verMissed[x] > 0 )    // In fact, it should be always greater than zero here unless there is a bug
            pDecoder->verMissed[x]--;
        if ( pDecoder->globalMissed > 0 )    // In fact, it should be always greater than zero here unless there is a bug
            pDecoder->globalMissed--;
    } else {
        /* Recovery shard */
        uint8_t recoveryIndex = index - pParam->OriginalCount;
        pDecoder->shards[index].pData = (uint8_t *)(pShard + 1);   // skip index byte
        pDecoder->shards[index].lrcIndex = pParam->TotalOriginalCount + recoveryIndex;
        if ( recoveryIndex >= pParam->FirstHorRecoveryIndex && recoveryIndex < pParam->FirstHorRecoveryIndex + pParam->VerLocalCount ) {
            /* One of horizonal recovery shards */
            pDecoder->shards[index].decodeIndex = HOR_DECODE_INDEX(pParam);
            y = recoveryIndex - pParam->FirstHorRecoveryIndex;
            x = -1;
        } else if ( recoveryIndex >= pParam->FirstVerRecoveryIndex && recoveryIndex < pParam->FirstVerRecoveryIndex + pParam->HorLocalCount ) {
            /* One of vertical recovery shards */
            pDecoder->shards[index].decodeIndex = VER_DECODE_INDEX(pParam);
            x = recoveryIndex - pParam->FirstVerRecoveryIndex;
            y = -1;
        } else if ( recoveryIndex >= pParam->FirstGlobalRecoveryIndex && recoveryIndex < pParam->FirstGlobalRecoveryIndex + pParam->GlobalRecoveryCount ) {
            /* One of global recovery shard */
            pDecoder->shards[index].decodeIndex = GLOBAL_DECODE_INDEX(pParam, recoveryIndex - pParam->FirstGlobalRecoveryIndex);
            pDecoder->numGlobalRecovery++;
            x = -1;
            y = -1;
        } else if ( recoveryIndex == pParam->LocalRecoveryOfGlobalRecoveryIndex ) {
            pDecoder->shards[index].decodeIndex = HOR_DECODE_INDEX(pParam);   // XOR for local recovery shard for global recovery shards
            x = -1;
            y = -1;
        } else
            return -3;  // Impossible branch, because the value of index has been checked above
    }

    short x1 = x;
    short y1 = y;
    while ( y1 >= 0 ) {
        x1 = CheckAndRecoverHor(pDecoder, y1);
        if ( x1 < 0 )
            break;
        y1 = CheckAndRecoverVer(pDecoder, x1);
    }

    x1 = x;
    y1 = y;
    while ( x1 >= 0 ) {
        y1 = CheckAndRecoverVer(pDecoder, x1);
        if ( y1 < 0 )
            break;
        x1 = CheckAndRecoverHor(pDecoder, y1);
    }

    CheckAndRecoverGlobal(pDecoder);

    if ( pDecoder->globalMissed <= 0 ) {
        FreeHandle(handle);
        return 1;   // ALl data already been repaired
    }
    if ( pDecoder->globalMissed > pDecoder->totalGlobalRecovery )
        return 0;

    /* ALl data can be repaired by global recovery shards */
    short globalIndex = pParam->OriginalCount + pParam->FirstGlobalRecoveryIndex;
    for (i = 0; i < pParam->OriginalCount; i++) {
        if ( !SHARD_EXISTED(pDecoder, i) ) {
            /* Found one missing shard that need be repaired */
            while ( !SHARD_EXISTED(pDecoder, globalIndex) ) {
                if ( ++globalIndex == pParam->OriginalCount + pParam->LocalRecoveryOfGlobalRecoveryIndex )
                    globalIndex++;
                if ( globalIndex > GLOBAL_FROM_VER_INDEX(pParam) )
                    return 0;   // This branch is impossible unless there is bug
            }
            pDecoder->shards[i].pData = pDecoder->pDecodedData + i * pParam->BlockBytes;
            memcpy(pDecoder->shards[i].pData, pDecoder->shards[globalIndex].pData, pParam->BlockBytes);
            pDecoder->shards[i].lrcIndex =  pDecoder->shards[globalIndex].lrcIndex;
            pDecoder->shards[i].decodeIndex =  pDecoder->shards[globalIndex].decodeIndex;

            globalIndex++;
        }
    }

    cm256_encoder_params params;
    params.BlockBytes = pParam->BlockBytes;
    params.TotalOriginalCount = pParam->TotalOriginalCount;
    params.FirstElement = 0;
    params.OriginalCount = pParam->OriginalCount;
    params.RecoveryCount = pDecoder->globalMissed;
    params.Step = 1;

    if ( cm256_decode(params, pDecoder->shards) )
        return 0;   // Decode fails

    FreeHandle(handle);
    return 1;   // Data retrived
}

/*
 * Abandon a decode process
 */
extern void FreeHandle(short handle)
{
    if (handle >= 0 && handle < numDecoders) {
        decoders[handle].bIsUsed = false;
        if ( NULL != decoders[handle].pBuffer )
            free(decoders[handle].pBuffer);
        decoders[handle].pDecodedData = NULL;
    }
}

short GetHorLocalCount(short originalCount)
{
    return originalCount >= 64 ? 8 : sqrt(originalCount);
}

void InitialParam(CM256LRC *pParam, unsigned short originalCount, unsigned shardSize, bool bIndexByte)
{
    pParam->bIndexByte = bIndexByte;
    pParam->BlockBytes = bIndexByte ? shardSize - 1 : shardSize;
    pParam->OriginalCount = originalCount;
    pParam->GlobalRecoveryCount = globalRecoveryCount;
    pParam->HorLocalCount = GetHorLocalCount(originalCount);
    pParam->VerLocalCount = (originalCount + pParam->HorLocalCount - 1) / pParam->HorLocalCount;
    pParam->TotalOriginalCount = pParam->HorLocalCount * pParam->VerLocalCount;
    pParam->FirstHorRecoveryIndex = 0;
    pParam->FirstVerRecoveryIndex = pParam->VerLocalCount;
    pParam->FirstGlobalRecoveryIndex = pParam->VerLocalCount + pParam->HorLocalCount;
    pParam->LocalRecoveryOfGlobalRecoveryIndex = pParam->FirstGlobalRecoveryIndex + pParam->GlobalRecoveryCount;
    pParam->TotalRecoveryCount = pParam->LocalRecoveryOfGlobalRecoveryIndex + 1;
}
