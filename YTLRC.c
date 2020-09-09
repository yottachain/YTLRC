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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "cm256.h"
#include "YTLRC.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>


typedef struct
{
    unsigned long magic;
    CM256LRC param;
    uint8_t *pDecodedData;

    unsigned short numShards;
    CM256Block blocks[MAXSHARDS];
    short horMissed[MAXSHARDS / MAXHORCOUNT]; // number of missed shards in each horizonal local groups of original data, ignore missed recovery shard(s)
    short verMissed[MAXHORCOUNT];             // number of missed shards in each vertical local groups of original data, ignore missed recovery shard(s)
    short globalMissed;                       // number of missed shards of original data, ignore missed recovery shard(s)
    short numGlobalRecovery;
    short totalGlobalRecovery; // count in global recovery shard from horizonal recovery shards and vertical recovery shards
    short numHorRecovery, numVerRecovery;

    uint8_t *pBuffer; // Buffer for at most 4 shards: one for repair global recovery shard,
                      // one for additional global recovery shard from horizonal recovery shards,
                      // one for additional global recovery shard from vertical recovery shard,
                      // one for zero shard
} DecoderLRC;
#define SHARD_EXISTED(pDecoder, index) (NULL != pDecoder->blocks[index].pData)
#define DECODE_MAGIC 0x59541224

static short globalRecoveryCount = 10;



static int WriteAddrToFile(void *addr, char *entry, char *filename)
{
	return;
    int fd;
	char addrstr[10];
	char des[256];
	char filelog[] = "/root/c_mallocytlrc0";

	filename = filelog;
	unsigned long  addrint = (unsigned long)addr;
	//ultoa(addrint,addrstr,10);
	//char str[20];
 	//double f=14.309948;
	sprintf(addrstr,"%lu",addrint);
	strcpy(des,entry);
	strcat(des," ");
	strcat(des,addrstr);
	strcat(des,"\n\r");
	fd = open(filename,O_RDWR|O_CREAT|O_APPEND);	
	write(fd,des,strlen(des));
	close(fd);
	//sleep(2);
	return 0;
}

static inline uint8_t *GlobalRecoveryBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer;
}
static inline uint8_t *GlobalFromHorBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer + pDecoder->param.BlockBytes + 1;
}
static inline uint8_t *GlobalFromVerBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer + 2 * (pDecoder->param.BlockBytes + 1);
}
static inline uint8_t *ZeroBuf(DecoderLRC *pDecoder)
{
    return pDecoder->pBuffer + 3 * (pDecoder->param.BlockBytes + 1);
}

typedef struct
{
    unsigned long magic;
    short iLost;
    CM256LRC param;
    uint8_t *pRepairedData;
    enum
    {
        INIT_REBUILD,
        HOR_REBUILD,
        VER_REBUILD,
        HOR_RECOVERY_REBUILD,
        VER_RECOVERY_REBUILD,
        GLOBAL_RECOVERY_REBUILD,
        GLOBAL_REBUILD
    } stage;
    enum
    {
        UNKNOWN,
        EXISTED,
        LOST,
        REQUEST
    } shardStatus[MAXSHARDS];
    short remainShards;               // Used for HOR_REBUILD, VER_REBUILD, HOR_RECOVERY_REBUILD,  VER_RECOVERY_REBUILD, GLOBAL_RECOVERY_REBUILD
    short numShards;                  // Number of existing shards
    const uint8_t *shards[MAXSHARDS]; // Existing shards
    DecoderLRC *pDecoder;             // Used for GLOBAL_REBUILD
    uint8_t *pDecodedData;            // Used for GLOBAL_REBUILD
} Rebuilder;
#define REBUILD_MAGIC 0x59542019

extern void InitialParam(CM256LRC *pParam, unsigned short originalCount, unsigned shardSize, bool bIndexByte);
/*
 * Initialize
 * numGlobalRecoveryCount: number of global recovery shards
 * maxHandles: maximum of decoders or rebuilders working at same time
 * return: 0 if fails
 */
extern short LRC_Initial(short n)
{
    //short i;
    if (cm256_init() || n <= 2)
        return false;

    globalRecoveryCount = n - 2;

    return true;
}

/*
 * Encode original data, return recovery shards
 * originalShards: shards for original data, 1st byte of each shard is its index
 * shardSize: size of each shard
 * originalCount: number of shards of original data
 * pRecoveryData: required at least MAXRECOVERYSHARDS*shardSize space, return recovery shards after encoding,
 *                the leading byte of each shard is index of this shard
 * return: number of recovery shards, <=0 fails
 */
extern short LRC_Encode(const void *originalShards[], unsigned short originalCount, unsigned long shardSize, void *pRecoveryData)
{
    CM256LRC param;
    CM256Block blocks[MAXSHARDS];
    short i;
    uint8_t *pZeroData = NULL;

    if (NULL == originalShards || originalCount <= 0 || originalCount > 230 || shardSize <= 1 || NULL == pRecoveryData)
        return -1;
    InitialParam(&param, originalCount, shardSize, true);
    for (i = 0; i < originalCount; i++)
        blocks[i].pData = (uint8_t *)originalShards[i] + 1; // Ignore the index byte
    pZeroData = malloc(shardSize - 1 + 8);
	WriteAddrToFile(pZeroData,"pZeroData","/root/c_malloc");
    if (NULL == pZeroData)
        return -2;
    memset(pZeroData, 0, shardSize - 1);
    for (i = originalCount; i < param.TotalOriginalCount; i++)
    {
        blocks[i].pData = pZeroData;
        blocks[i].lrcIndex = i;
        blocks[i].decodeIndex = i;
    }

    int ret = cm256_encode(param, blocks, pRecoveryData);

	WriteAddrToFile(pZeroData,"free_pZeroData","/root/c_free");
	printf("free_pZeroData=%p \n",pZeroData);
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
extern void *LRC_BeginDecode(unsigned short originalCount, unsigned long shardSize, void *pData)
{
    short j;
    if (originalCount <= 0 || originalCount >= MAXSHARDS || NULL == pData)
        return NULL;

    DecoderLRC *pDecoder = malloc(sizeof(DecoderLRC));
	WriteAddrToFile(pDecoder,"pDecoder","/root/c_malloc");
	sleep(2);
    if (NULL == pDecoder)
        return NULL;
    pDecoder->magic = DECODE_MAGIC;
    InitialParam(&pDecoder->param, originalCount, shardSize, true);
	sleep(2);
    pDecoder->pDecodedData = pData;
    pDecoder->numShards = 0;
    for (j = 0; j < MAXSHARDS; j++)
        pDecoder->blocks[j].pData = NULL;
    pDecoder->pBuffer = malloc(4 * shardSize);
	WriteAddrToFile(pDecoder->pBuffer,"pDecoder->pBuffer","/root/c_malloc");
	sleep(2);
	if (NULL == pDecoder->pBuffer)
    {
    	WriteAddrToFile(pDecoder,"free_pDecoder","/root/c_free");
        free(pDecoder);
		sleep(2);
        return NULL;
    }
    memset(ZeroBuf(pDecoder), 0, shardSize); // The last shard is zero shard
    for (j = pDecoder->param.OriginalCount; j < pDecoder->param.TotalOriginalCount; j++)
    {
        pDecoder->blocks[j].pData = ZeroBuf(pDecoder);
        pDecoder->blocks[j].lrcIndex = j;
        pDecoder->blocks[j].decodeIndex = j;
    }

    pDecoder->globalMissed = pDecoder->param.OriginalCount;
    pDecoder->numGlobalRecovery = 0;
    pDecoder->totalGlobalRecovery = 0;
    pDecoder->numHorRecovery = 0;
    pDecoder->numVerRecovery = 0;
    for (j = 0; j < pDecoder->param.VerLocalCount; j++)
        pDecoder->horMissed[j] = pDecoder->param.HorLocalCount;
    pDecoder->horMissed[pDecoder->param.VerLocalCount - 1] -= pDecoder->param.TotalOriginalCount - pDecoder->param.OriginalCount;
    for (j = 0; j < pDecoder->param.HorLocalCount; j++)
        pDecoder->verMissed[j] = pDecoder->param.VerLocalCount;
    for (j = pDecoder->param.OriginalCount; j < pDecoder->param.TotalOriginalCount; j++)
        pDecoder->verMissed[j % pDecoder->param.HorLocalCount]--;

    return pDecoder;
}

/* Recover a horizaonal local group when possible, return the x coordinate of recovered shard, <0 means failed */
static short CheckAndRecoverHor(DecoderLRC *pDecoder, short y)
{
    short x;
    CM256LRC *pParam = &pDecoder->param;
    if (pDecoder->horMissed[y] != 1 || !SHARD_EXISTED(pDecoder, HOR_RECOVERY_INDEX(pParam, y)))
        return -1;
    /* Only miss one shard in this horizonal local group and the recovery shard of this group exists, recover the missing shard */
    unsigned short index2 = y * pParam->HorLocalCount;
    for (x = 0; x < pParam->HorLocalCount; x++, index2++)
    {
        if (!SHARD_EXISTED(pDecoder, index2))
        {
            /* Found the missing shard, recover it */
            pDecoder->blocks[index2].lrcIndex = HOR_RECOVERY_INDEX(pParam, y);
            pDecoder->blocks[index2].decodeIndex = HOR_DECODE_INDEX(pParam);
            pDecoder->blocks[index2].pData = pDecoder->pDecodedData + index2 * pParam->BlockBytes;
            /* Copy horizontal recovery shard data to missing shard, it will be recovered to decoded data by cm256_decode */
            memcpy(pDecoder->blocks[index2].pData, pDecoder->blocks[HOR_RECOVERY_INDEX(pParam, y)].pData, pParam->BlockBytes);

            cm256_encoder_params params;
            params.BlockBytes = pParam->BlockBytes;
            params.TotalOriginalCount = pParam->TotalOriginalCount;
            params.FirstElement = y * pParam->HorLocalCount;
            params.OriginalCount = pParam->HorLocalCount;
            params.RecoveryCount = 1;
            params.Step = 1;

            if (cm256_decode(params, pDecoder->blocks) != 0)
            {
                pDecoder->blocks[index2].pData = NULL; // Decode error, it is not recovered
                return -2;
            }

            if (pDecoder->horMissed[y] > 0) // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->horMissed[y]--;
            if (pDecoder->verMissed[x] > 0) // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->verMissed[x]--;
            if (pDecoder->globalMissed > 0) // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->globalMissed--;

            return x;
        }
    }
    return -3; // This branch is imppossible unless there is a bug
}

/* Recover a vertical local group when possible, return the y coordinate of recovered shard, <0 means failed */
static short CheckAndRecoverVer(DecoderLRC *pDecoder, short x)
{
    short y;
    CM256LRC *pParam = &pDecoder->param;
    if (pDecoder->verMissed[x] != 1 || !SHARD_EXISTED(pDecoder, VER_RECOVERY_INDEX(pParam, x)))
        return -1;
    /* Only miss one shard in this vertical local group and the recovery shard of this group exists, recover the missing shard */
    unsigned short index2 = x;
    for (y = 0; y < pParam->VerLocalCount; y++)
    {
        if (!SHARD_EXISTED(pDecoder, index2))
        {
            /* Found the missing shard, recover it */
            pDecoder->blocks[index2].lrcIndex = VER_RECOVERY_INDEX(pParam, x);
            pDecoder->blocks[index2].decodeIndex = VER_DECODE_INDEX(pParam);
            pDecoder->blocks[index2].pData = pDecoder->pDecodedData + index2 * pParam->BlockBytes;
            /* Copy vertical recovery shard data to missing shard, it will be recovered to decoded data by cm256_decode */
            memcpy(pDecoder->blocks[index2].pData, pDecoder->blocks[VER_RECOVERY_INDEX(pParam, x)].pData, pParam->BlockBytes);

            cm256_encoder_params params;
            params.BlockBytes = pParam->BlockBytes;
            params.TotalOriginalCount = pParam->TotalOriginalCount;
            params.FirstElement = x;
            params.OriginalCount = pParam->VerLocalCount;
            params.RecoveryCount = 1;
            params.Step = pParam->HorLocalCount;

            if (cm256_decode(params, pDecoder->blocks) != 0)
            {
                pDecoder->blocks[index2].pData = NULL; // Decode error, it is not recovered
                return -2;
            }

            /* Recovery success */
            if (pDecoder->horMissed[y] > 0) // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->horMissed[y]--;
            if (pDecoder->verMissed[x] > 0) // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->verMissed[x]--;
            if (pDecoder->globalMissed > 0) // In fact, it should be always greater than zero here unless there is a bug
                pDecoder->globalMissed--;

            return y;
        }
        index2 += pParam->HorLocalCount;
    }
    return -3; // This branch is imppossible unless there is a bug
}

/* Recover local group of global recovery shards when possible */
static bool CheckAndRecoverGlobal(DecoderLRC *pDecoder)
{
    short i;
    bool ret = false;
    CM256LRC *pParam = &pDecoder->param;
    if (NULL == pDecoder->pBuffer)
        return false;
    if (pDecoder->numGlobalRecovery == pParam->GlobalRecoveryCount - 1 && SHARD_EXISTED(pDecoder, cm256_get_recovery_block_index(pParam, pParam->LocalRecoveryOfGlobalRecoveryIndex)))
    {
        /* Only miss one global recovery shard, recovery it from local recovery shard of global recovery shards */
        uint8_t *pBuf = GlobalRecoveryBuf(pDecoder);
        memcpy(pBuf, pDecoder->blocks[cm256_get_recovery_block_index(pParam, pParam->LocalRecoveryOfGlobalRecoveryIndex)].pData, pParam->BlockBytes);
        for (i = 0; i < pParam->GlobalRecoveryCount; i++)
        {
            short index = GLOBAL_RECOVERY_INDEX(pParam, i);
            if (!SHARD_EXISTED(pDecoder, index))
            {
                /* Found the missing shard, repair it */
                pDecoder->blocks[index].pData = pBuf;
                pDecoder->blocks[index].lrcIndex = index;
                pDecoder->blocks[index].decodeIndex = GLOBAL_DECODE_INDEX(pParam, i);
            }
            else
            {
                assert(pDecoder->blocks[index].pData != pBuf);
                gf256_add_mem(pBuf, pDecoder->blocks[index].pData, pParam->BlockBytes);
            }
        }
        pDecoder->numGlobalRecovery++;
        pDecoder->totalGlobalRecovery++;
        ret = true;
    }

    CM256Block *pBlock = &pDecoder->blocks[GLOBAL_FROM_HOR_INDEX(pParam)];
    if (pDecoder->numHorRecovery == pParam->VerLocalCount && NULL == pBlock->pData)
    {
        /* There is an additional global recovery shard from horizonal recovery shards */
        uint8_t *pBuf = GlobalFromHorBuf(pDecoder);
        memcpy(pBuf, pDecoder->blocks[HOR_RECOVERY_INDEX(pParam, 0)].pData, pParam->BlockBytes);
        for (i = 1; i < pParam->VerLocalCount; i++)
        {
            gf256_add_mem(pBuf, pDecoder->blocks[HOR_RECOVERY_INDEX(pParam, i)].pData, pParam->BlockBytes);
        }
        pBlock->pData = pBuf;
        pBlock->lrcIndex = GLOBAL_FROM_HOR_INDEX(pParam);
        pBlock->decodeIndex = HOR_DECODE_INDEX(pParam);

        pDecoder->totalGlobalRecovery++;
        ret = true;
    }

    pBlock = &pDecoder->blocks[GLOBAL_FROM_VER_INDEX(pParam)];
    if (pDecoder->numVerRecovery == pParam->HorLocalCount && NULL == pBlock->pData)
    {
        /* There is an additional global recovery shard from vertical recovery shards */
        uint8_t *pBuf = GlobalFromVerBuf(pDecoder);
        memcpy(pBuf, pDecoder->blocks[VER_RECOVERY_INDEX(pParam, 0)].pData, pParam->BlockBytes);
        for (i = 1; i < pParam->HorLocalCount; i++)
            gf256_add_mem(pBuf, pDecoder->blocks[VER_RECOVERY_INDEX(pParam, i)].pData, pParam->BlockBytes);
        pBlock->pData = pBuf;
        pBlock->lrcIndex = GLOBAL_FROM_VER_INDEX(pParam);
        pBlock->decodeIndex = VER_DECODE_INDEX(pParam);

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
extern short LRC_Decode(void *handle, const void *pData)
{
    short i;
    short x, y;

    if (NULL == handle || NULL == pData)
        return -1;
    DecoderLRC *pDecoder = handle;
    if (DECODE_MAGIC != pDecoder->magic)
        return -1;
    uint8_t *pShard = (uint8_t *)pData;
    uint8_t index = pShard[0];
    if (index > pDecoder->param.OriginalCount + pDecoder->param.TotalRecoveryCount)
        return -2;

    CM256LRC *pParam = &pDecoder->param;
    if (index < pParam->OriginalCount)
    {
        /* Original data */
        if (SHARD_EXISTED(pDecoder, index))
            return 0; // Already calculated or received
        pDecoder->blocks[index].lrcIndex = index;
        pDecoder->blocks[index].decodeIndex = index;
        pDecoder->blocks[index].pData = pDecoder->pDecodedData + index * pParam->BlockBytes;
        memcpy(pDecoder->blocks[index].pData, pShard + 1, pParam->BlockBytes); // Copy to destinaltion

        y = index / pParam->HorLocalCount;
        x = index % pParam->HorLocalCount;
        if (pDecoder->horMissed[y] > 0) // In fact, it should be always greater than zero here unless there is a bug
            pDecoder->horMissed[y]--;
        if (pDecoder->verMissed[x] > 0) // In fact, it should be always greater than zero here unless there is a bug
            pDecoder->verMissed[x]--;
        if (pDecoder->globalMissed > 0) // In fact, it should be always greater than zero here unless there is a bug
            pDecoder->globalMissed--;
    }
    else
    {
        /* Recovery shard */
        uint8_t recoveryIndex = index - pParam->OriginalCount;
        index = cm256_get_recovery_block_index(pParam, recoveryIndex);
        if (SHARD_EXISTED(pDecoder, index))
            return 0;                               // Already calculated or received
        pDecoder->blocks[index].pData = pShard + 1; // skip index byte
        pDecoder->blocks[index].lrcIndex = index;
        if (recoveryIndex >= pParam->FirstHorRecoveryIndex && recoveryIndex < pParam->FirstHorRecoveryIndex + pParam->VerLocalCount)
        {
            /* One of horizonal recovery shards */
            pDecoder->numHorRecovery++;
            pDecoder->blocks[index].decodeIndex = HOR_DECODE_INDEX(pParam);
            y = recoveryIndex - pParam->FirstHorRecoveryIndex;
            x = -1;
        }
        else if (recoveryIndex >= pParam->FirstVerRecoveryIndex && recoveryIndex < pParam->FirstVerRecoveryIndex + pParam->HorLocalCount)
        {
            /* One of vertical recovery shards */
            pDecoder->numVerRecovery++;
            pDecoder->blocks[index].decodeIndex = VER_DECODE_INDEX(pParam);
            x = recoveryIndex - pParam->FirstVerRecoveryIndex;
            y = -1;
        }
        else if (recoveryIndex >= pParam->FirstGlobalRecoveryIndex && recoveryIndex < pParam->FirstGlobalRecoveryIndex + pParam->GlobalRecoveryCount)
        {
            /* One of global recovery shard */
            pDecoder->blocks[index].decodeIndex = GLOBAL_DECODE_INDEX(pParam, recoveryIndex - pParam->FirstGlobalRecoveryIndex);
            pDecoder->numGlobalRecovery++;
            pDecoder->totalGlobalRecovery++;
            x = -1;
            y = -1;
        }
        else if (recoveryIndex == pParam->LocalRecoveryOfGlobalRecoveryIndex)
        {
            pDecoder->blocks[index].decodeIndex = HOR_DECODE_INDEX(pParam); // XOR for local recovery shard for global recovery shards
            x = -1;
            y = -1;
        }
        else
            return -3; // Impossible branch, because the value of index has been checked above
    }
    pDecoder->numShards++;

    short x1 = x;
    short y1 = y;
    while (y1 >= 0)
    {
        x1 = CheckAndRecoverHor(pDecoder, y1);
        if (x1 < 0)
            break;
        y1 = CheckAndRecoverVer(pDecoder, x1);
    }

    x1 = x;
    y1 = y;
    while (x1 >= 0)
    {
        y1 = CheckAndRecoverVer(pDecoder, x1);
        if (y1 < 0)
            break;
        x1 = CheckAndRecoverHor(pDecoder, y1);
    }

    CheckAndRecoverGlobal(pDecoder);

    if (pDecoder->globalMissed <= 0)
        return 1; // ALl data already been repaired
    if (pDecoder->globalMissed > pDecoder->totalGlobalRecovery)
        return 0;

    /* ALl data can be repaired by global recovery shards */
    short globalIndex = GLOBAL_RECOVERY_INDEX(pParam, 0);
    for (i = 0; i < pParam->OriginalCount; i++)
    {
        if (!SHARD_EXISTED(pDecoder, i))
        {
            /* Found one missing shard that need be repaired */
            while (!SHARD_EXISTED(pDecoder, globalIndex))
            {
                if (++globalIndex == cm256_get_recovery_block_index(pParam, pParam->LocalRecoveryOfGlobalRecoveryIndex))
                    globalIndex++;
                if (globalIndex > MAX_INDEX(pParam))
                    return 0; // This branch is impossible unless there is bug
            }
            pDecoder->blocks[i].pData = pDecoder->pDecodedData + i * pParam->BlockBytes;
            memcpy(pDecoder->blocks[i].pData, pDecoder->blocks[globalIndex].pData, pParam->BlockBytes);
            pDecoder->blocks[i].lrcIndex = pDecoder->blocks[globalIndex].lrcIndex;
            pDecoder->blocks[i].decodeIndex = pDecoder->blocks[globalIndex].decodeIndex;

            globalIndex++;
            if (globalIndex == cm256_get_recovery_block_index(pParam, pParam->LocalRecoveryOfGlobalRecoveryIndex))
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

    return !cm256_decode(params, pDecoder->blocks);
}

/*
 * Abandon a decode or rebuild process
 */
extern short LRC_FreeHandle(void *handle)
{
    if (NULL == handle)
        return false;

    DecoderLRC *pDecoder = handle;
    if (DECODE_MAGIC == pDecoder->magic)
    {
        if (NULL != pDecoder->pBuffer){
			WriteAddrToFile(pDecoder->pBuffer, "free_pDecoder->pBuffer", "/root/c_free");
            free(pDecoder->pBuffer);
		    sleep(2);
        	}
		WriteAddrToFile(pDecoder, "pDecoder", "/root/c_free");
		printf("pDecoder=%p\n",pDecoder);
        free(pDecoder);
		sleep(2);
        return true;
    }

    Rebuilder *pRebuilder = handle;
    if (REBUILD_MAGIC == pRebuilder->magic)
    {
        if (NULL != pRebuilder->pDecoder)
            LRC_FreeHandle(pRebuilder->pDecoder);
        if (NULL != pRebuilder->pDecodedData){
			WriteAddrToFile(pRebuilder->pDecodedData, "free_pRebuilder->pDecodedData", "/root/c_free");        	
            free(pRebuilder->pDecodedData);
			}
		WriteAddrToFile(pRebuilder, "free_pRebuilder", "/root/c_free");
        free(pRebuilder);		
        return true;
    }
    return false;
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

/*
 * Begin a rebuild process
 * originalCount: number of shards of original data
 * iLost: order of lost shard
 * shardSize: size of each shard
 * pData: the buffer for rebuilt shard, at least shardSize length
 * return: handle of rebuild process, NULL fails
 */
extern void *LRC_BeginRebuild(unsigned short originalCount, unsigned short iLost, unsigned long shardSize, void *pData)
{
    short j;
    CM256LRC param;
    if (originalCount <= 0 || originalCount >= MAXSHARDS)
        return NULL;
    InitialParam(&param, originalCount, shardSize, true);
    if (iLost >= originalCount + param.TotalRecoveryCount)
        return NULL;

    Rebuilder *pRebuilder = malloc(sizeof(Rebuilder));
	WriteAddrToFile(pRebuilder, "pRebuilder", "/root/c_malloc");
	sleep(2);
    if (NULL == pRebuilder)
        return NULL;
    pRebuilder->magic = REBUILD_MAGIC;
    pRebuilder->iLost = iLost;
    pRebuilder->pDecoder = NULL;
    pRebuilder->stage = INIT_REBUILD;
    pRebuilder->pRepairedData = pData;
    *pRebuilder->pRepairedData++ = iLost; // Index byte

    memset(pRebuilder->shardStatus, UNKNOWN, sizeof(pRebuilder->shardStatus));
    pRebuilder->shardStatus[iLost] = LOST;
    for (j = 0; j < MAXSHARDS; j++)
        pRebuilder->shards[j] = NULL;
    pRebuilder->numShards = 0;
    pRebuilder->pDecodedData = NULL;
    pRebuilder->param = param;

    return pRebuilder;
}

/*
 * Get next shard list for rebuild the lost shard. 
 * Invoking this function means the remaning shards of last list are lost.
 * handle: handle of rebuild process
 * pList: output, at least 256 bytes space, return new list of required shards
 * return: number of shards in new list. 0 if no way to rebuild, <0 if something wrong
 */
extern short LRC_NextRequestList(void *handle, unsigned char *pList)
{
    short i, j;
    short numRequest;
    if (NULL == handle || NULL == pList)
        return -1;

    Rebuilder *pRebuilder = handle;
    if (REBUILD_MAGIC != pRebuilder->magic)
        return -2;

    CM256LRC *pParam = &pRebuilder->param;
    short iLost = pRebuilder->iLost;
    switch (pRebuilder->stage)
    {
    case INIT_REBUILD:
        if (iLost < pParam->OriginalCount)
        {
            /* Original shard */
            pRebuilder->stage = HOR_REBUILD;
            numRequest = 0;
            short y = iLost / pParam->HorLocalCount;
            short n = y * pParam->HorLocalCount;
            for (j = 0; j < pParam->HorLocalCount; j++)
            {
                if (n + j == iLost)
                    pList[numRequest++] = pParam->OriginalCount + pParam->FirstHorRecoveryIndex + y; // Horizonal local recovery shard
                else if (n + j < pParam->OriginalCount)
                    pList[numRequest++] = n + j;
            }
            pRebuilder->remainShards = numRequest;
        }
        else
        {
            short recoveryIndex = iLost - pParam->OriginalCount;
            if (recoveryIndex >= pParam->FirstHorRecoveryIndex && recoveryIndex < pParam->FirstHorRecoveryIndex + pParam->VerLocalCount)
            {
                /* One of horizonal recovery shards */
                pRebuilder->stage = HOR_RECOVERY_REBUILD;
                numRequest = 0;
                short y = recoveryIndex - pParam->FirstHorRecoveryIndex;
                short n = y * pParam->HorLocalCount;
                for (j = 0; j < pParam->HorLocalCount && n + j < pParam->OriginalCount; j++)
                    pList[numRequest++] = n + j;
                pRebuilder->remainShards = numRequest;
            }
            else if (recoveryIndex >= pParam->FirstVerRecoveryIndex && recoveryIndex < pParam->FirstVerRecoveryIndex + pParam->HorLocalCount)
            {
                /* One of vertical recovery shards */
                pRebuilder->stage = VER_RECOVERY_REBUILD;
                pRebuilder->remainShards = numRequest = pParam->VerLocalCount;
                short x = recoveryIndex - pParam->FirstVerRecoveryIndex;
                numRequest = 0;
                for (j = 0; j < pParam->VerLocalCount; j++)
                {
                    short iRequest = j * pParam->HorLocalCount + x;
                    if (iRequest < pParam->OriginalCount)
                        pList[numRequest++] = iRequest;
                }
                pRebuilder->remainShards = numRequest;
            }
            else if ((recoveryIndex >= pParam->FirstGlobalRecoveryIndex && recoveryIndex < pParam->FirstGlobalRecoveryIndex + pParam->GlobalRecoveryCount) || recoveryIndex == pParam->LocalRecoveryOfGlobalRecoveryIndex)
            {
                /* One of global recovery shard */
                pRebuilder->stage = GLOBAL_RECOVERY_REBUILD;
                pRebuilder->remainShards = numRequest = pParam->GlobalRecoveryCount;
                short n = pParam->OriginalCount + pParam->FirstGlobalRecoveryIndex;
                for (j = 0; j < pParam->GlobalRecoveryCount; j++)
                {
                    if (n + j == iLost)
                        pList[j] = pParam->OriginalCount + pParam->LocalRecoveryOfGlobalRecoveryIndex;
                    else
                        pList[j] = n + j;
                }
            }
            else
                return -2; // Impossible branch, because the value of index has been checked above
        }
        memset(pRebuilder->pRepairedData, 0, pRebuilder->param.BlockBytes);
        break;

    case HOR_REBUILD:
        pRebuilder->stage = VER_REBUILD;
        unsigned short x = pRebuilder->iLost % pRebuilder->param.HorLocalCount;
        unsigned short index = x;
        numRequest = 0;
        for (i = 0; i < pRebuilder->param.VerLocalCount; i++)
        {
            if (index == pRebuilder->iLost)
                pList[numRequest++] = pRebuilder->param.OriginalCount + pRebuilder->param.FirstVerRecoveryIndex + x;
            else if (index < pParam->OriginalCount)
                pList[numRequest++] = index;
            index += pRebuilder->param.HorLocalCount;
        }
        pRebuilder->remainShards = numRequest;
        memset(pRebuilder->pRepairedData, 0, pRebuilder->param.BlockBytes);
        break;

    case VER_REBUILD:
    case HOR_RECOVERY_REBUILD:
    case VER_RECOVERY_REBUILD:
    case GLOBAL_RECOVERY_REBUILD:
        /* Directly go to global recovery process if neither horizonal and vertical recovery works. Can be optimized later */
        pRebuilder->stage = GLOBAL_REBUILD;
        numRequest = 0;
        unsigned short nLost = 0;
        for (i = 0; i < pRebuilder->param.OriginalCount; i++)
        {
            if (LOST == pRebuilder->shardStatus[i])
                nLost++;
            else if (EXISTED != pRebuilder->shardStatus[i])
                pList[numRequest++] = i;
        }
        if (nLost > pRebuilder->param.TotalRecoveryCount)
            return 0; // Unable to repair
        /* Request all recovery shards, not only global ones */
        for (i = 0; i < pRebuilder->param.TotalRecoveryCount; i++)
        {
            j = pRebuilder->param.OriginalCount + i;
            if (LOST != pRebuilder->shardStatus[j] && EXISTED != pRebuilder->shardStatus[j])
                pList[numRequest++] = j;
        }

        pRebuilder->pDecodedData = malloc((pRebuilder->param.TotalOriginalCount + 1) * pRebuilder->param.BlockBytes); // Last block is reserved for figuring local recovery shard for global recovery shards
		WriteAddrToFile(pRebuilder->pDecodedData, "pRebuilder->pDecodedData", "/root/c_malloc");
		sleep(2);
		if (NULL == pRebuilder->pDecodedData)
            return -4;
        pRebuilder->pDecoder = LRC_BeginDecode(pRebuilder->param.OriginalCount, pRebuilder->param.BlockBytes + 1, pRebuilder->pDecodedData);
        if (NULL == pRebuilder->pDecoder)
            return -5;
        for (i = 0; i < pRebuilder->numShards; i++)
            LRC_Decode(pRebuilder->pDecoder, pRebuilder->shards[i]);
        break;

    case GLOBAL_REBUILD:
        return 0; // No way to rebuild

    default:
        return -3;
    }
    for (j = 0; j < numRequest; j++)
        pRebuilder->shardStatus[pList[j]] = REQUEST;
    return numRequest;
}

/*
 * Provide one shard for rebuilding lost shards
 * handle: handle of rebuild process
 * pShard: shard data
 * return: >0 if rebuilding is done, repaired data in the buffer provided at beginning of rebuilding process, automatically free handle, 0 if more shards required, <0 if something wrong
 */
extern short LRC_OneShardForRebuild(void *handle, const void *pShardData)
{
    short i = 0;
    uint8_t matrixElement;
    if (NULL == handle || NULL == pShardData)
        return -1;
    Rebuilder *pRebuilder = handle;
    if (REBUILD_MAGIC != pRebuilder->magic)
        return -1;

    CM256LRC *pParam = &pRebuilder->param;
    int blockBytes = pParam->BlockBytes;
    const uint8_t *pShard = pShardData;
    uint8_t index = *pShard;
    if (index >= pParam->OriginalCount + pParam->TotalRecoveryCount)
        return -2;
    if (REQUEST != pRebuilder->shardStatus[index])
        return -3;
    pRebuilder->shardStatus[index] = EXISTED;
    pRebuilder->shards[pRebuilder->numShards++] = pShard++; // pShard skips index byte
    switch (pRebuilder->stage)
    {
    case HOR_REBUILD:
    case HOR_RECOVERY_REBUILD:
    case GLOBAL_RECOVERY_REBUILD:
        //printf("-----------------------01-----------------\n");
        gf256_add_mem(pRebuilder->pRepairedData, pShard, blockBytes);
        if (--pRebuilder->remainShards <= 0)
            return 1;
        break;

    case VER_REBUILD:
        if (--pRebuilder->remainShards <= 0)
        {
            /* Enough to recover */
            assert(pRebuilder->numShards >= pParam->VerLocalCount - 1);
            CM256Block blocks[MAXSHARDS];
            unsigned short x = pRebuilder->iLost % pParam->HorLocalCount;
            unsigned short verRecoveryIndex = VER_RECOVERY_INDEX(pParam, x);
            for (i = 0; i < pRebuilder->numShards; i++)
            {
                uint8_t j = pRebuilder->shards[i][0];
                if (j < pParam->OriginalCount)
                {
                    /* Original shard */
                    blocks[j].lrcIndex = blocks[j].decodeIndex = j;
                    blocks[j].pData = (uint8_t *)pRebuilder->shards[i] + 1;
                }
                else if (j - pParam->OriginalCount + pParam->TotalOriginalCount == verRecoveryIndex)
                {
                    /* Vertical recovery shard for lost shard */
                    blocks[pRebuilder->iLost].lrcIndex = verRecoveryIndex;
                    blocks[pRebuilder->iLost].decodeIndex = pParam->TotalOriginalCount + 1;
                    blocks[pRebuilder->iLost].pData = pRebuilder->pRepairedData;
                    memcpy(pRebuilder->pRepairedData, (uint8_t *)pRebuilder->shards[i] + 1, pParam->BlockBytes);
                }
            }
            cm256_encoder_params params;
            params.BlockBytes = pParam->BlockBytes;
            params.TotalOriginalCount = pParam->TotalOriginalCount;
            params.FirstElement = x;
            if (0 == (pParam->OriginalCount % pParam->HorLocalCount) || x < (pParam->OriginalCount % pParam->HorLocalCount))
                params.OriginalCount = pParam->VerLocalCount;
            else
                params.OriginalCount = pParam->VerLocalCount - 1;
            params.RecoveryCount = 1;
            params.Step = pParam->HorLocalCount;

            if (cm256_decode(params, blocks) == 0)
                return 1;
            else
                return -7;
        }
        break;

    case VER_RECOVERY_REBUILD:
        matrixElement = GetMatrixElement(pParam->TotalOriginalCount + 1, pParam->TotalOriginalCount, index);
        gf256_muladd_mem(pRebuilder->pRepairedData, matrixElement, pShard, pParam->BlockBytes);
        if (--pRebuilder->remainShards <= 0)
            return 1;
        break;

    case GLOBAL_REBUILD:
        if (LRC_Decode(pRebuilder->pDecoder, pShardData) <= 0)
            break;
        /* Recovered original data */
        if (pRebuilder->iLost < pParam->OriginalCount)
        {
            /* Lost one of original shards */
            memcpy(pRebuilder->pRepairedData, pRebuilder->pDecodedData + pRebuilder->iLost * blockBytes, blockBytes);
            return 1;
        }
        /* Lost one of recovery shards */
        short recoveryIndex = pRebuilder->iLost - pParam->OriginalCount;
        memset(pRebuilder->pDecodedData + pParam->OriginalCount * blockBytes, 0, (pParam->TotalOriginalCount - pParam->OriginalCount) * blockBytes);
        if (recoveryIndex >= pParam->FirstHorRecoveryIndex && recoveryIndex < pParam->FirstHorRecoveryIndex + pParam->VerLocalCount)
        {
            /* Horizonal recovery shard */
            uint8_t *pData = pRebuilder->pDecodedData + (recoveryIndex - pParam->FirstHorRecoveryIndex) * pParam->HorLocalCount * blockBytes;
            gf256_addset_mem(pRebuilder->pRepairedData, pData, pData + blockBytes, blockBytes);
            pData += 2 * blockBytes;
            for (i = 2; i < pParam->HorLocalCount; i++)
            {
                gf256_add_mem(pRebuilder->pRepairedData, pData, blockBytes);
                pData += blockBytes;
            }
            return 1;
        }
        CM256Block blocks[MAXSHARDS];
        for (i = 0; i < pParam->TotalOriginalCount; i++)
        {
            blocks[i].pData = pRebuilder->pDecodedData + i * blockBytes;
            blocks[i].lrcIndex = i;
            blocks[i].decodeIndex = i;
        }

        cm256_encoder_params cmParam;
        cmParam.TotalOriginalCount = pParam->TotalOriginalCount;
        cmParam.BlockBytes = pParam->BlockBytes;
        if (recoveryIndex >= pParam->FirstVerRecoveryIndex && recoveryIndex < pParam->FirstVerRecoveryIndex + pParam->HorLocalCount)
        {
            /* One of vertical recovery shards */
            cmParam.OriginalCount = pParam->VerLocalCount;
            cmParam.Step = pParam->HorLocalCount;
            cmParam.RecoveryCount = 1;
            cmParam.FirstElement = recoveryIndex - pParam->FirstVerRecoveryIndex;
            CM256EncodeBlock(cmParam, blocks, cmParam.TotalOriginalCount + 1, pRebuilder->pRepairedData);
            return 1;
        }
        cmParam.OriginalCount = pParam->OriginalCount;
        cmParam.RecoveryCount = pParam->GlobalRecoveryCount;
        cmParam.FirstElement = 0;
        cmParam.Step = 1;
        if (recoveryIndex >= pParam->FirstGlobalRecoveryIndex && recoveryIndex < pParam->FirstGlobalRecoveryIndex + pParam->GlobalRecoveryCount)
        {
            /* One of global recovery shard */
            /* 1st recovery matrix is used for horizon recovery, 2nd matrix is used for vertical recovery, global recovery start from 2 */
            CM256EncodeBlock(cmParam, blocks, cmParam.TotalOriginalCount + recoveryIndex - pParam->FirstGlobalRecoveryIndex + 2, pRebuilder->pRepairedData);
            return 1;
        }
        if (recoveryIndex == pParam->LocalRecoveryOfGlobalRecoveryIndex)
        {
            /* Local recovery shard for global recovery shards */
            uint8_t *pGlobalRecovery = pRebuilder->pDecodedData + cmParam.TotalOriginalCount * blockBytes; // Space reserved for now
            memset(pRebuilder->pRepairedData, 0, blockBytes);
            for (i = 0; i < pParam->GlobalRecoveryCount; i++)
            {
                /* 1st recovery matrix is used for horizon recovery, 2nd matrix is used for vertical recovery, global recovery start from 2 */
                CM256EncodeBlock(cmParam, blocks, cmParam.TotalOriginalCount + i + 2, pGlobalRecovery); // Recover one global recovery shard
                gf256_add_mem(pRebuilder->pRepairedData, pGlobalRecovery, blockBytes);
            }
            return 1;
        }
        return -6; // Impossible branch

    default:
        return -5;
    }
    return 0;
}
