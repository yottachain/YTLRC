/*
    YottaChain Repairable Code API
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

#ifndef YTLRC_H
#define YTLRC_H

/*
 * Initialize
 * numGlobalRecoveryCount: number of global recovery shards
 * maxHandles: maximum of decoding processes or rebuilding processes working at same time
 * return: 0 if fails
 */
short LRC_Initial(short globalRecoveryCount, short maxHandles);

#define MAXRECOVERYSHARDS   36
/*
 * Encode original data, return recovery shards
 * originalShards: shards for original data, 1st byte of each shard is its index
 * shardSize: size of each shard
 * originalCount: number of shards of original data
 * pRecoveryData: required at least MAXRECOVERYSHARDS*shardSize space, return recovery shards after encoding,
 *                the leading byte of each shard is index of this shard
 * return: number of recovery shards, <=0 fails
 */
short LRC_Encode(const void *originalShards[], unsigned short originalCount, unsigned long shardSize, void *pRecoveryData);

/*
 * Begin of new decode process
 * originalCount: number of shards of original data
 * shardSize: size of each shard in byte including index byte
 * pData: require at least originalCount * (shardSize-1) space, return original data if success
 * return: handle of this decode process, <0 fails (such as exceed maxHandles)
 */
short LRC_BeginDecode(unsigned short originalCount, unsigned long shardSize, void *pData);

/*
 * Decode one shard for specific decode process
 * handle: handle of decode process
 * pShard: data of this shard
 * return: 0 if collected shards are not enough for decoding,  >0 success, <0 error
 */
short LRC_Decode(short handle, const void *pShard);

/*
 * Begin a rebuild process, call LRC_NextRequestList immediately to get requested shard list
 * originalCount: number of shards of original data
 * iLost: order of lost shard
 * shardSize: size of each shard
 * pData: the buffer for rebuilt shard, at least shardSize length
 * return: handle of rebuild process, <0 fails
 */
short LRC_BeginRebuild(unsigned short originalCount, unsigned short iLost, unsigned long shardSize, void *pData);

/*
 * Get next shard list for rebuild the lost shard. 
 * Invoking this function means the remaning shards of last list are lost.
 * handle: handle of rebuild process
 * pList: output, at least 256 bytes space, return new list of required shards
 * return: number of shards in new list. 0 if no way to rebuild, <0 if something wrong
 */
short LRC_NextRequestList(short handle, unsigned char *pList);

/*
 * Provide one shard for rebuilding lost shards
 * handle: handle of rebuild process
 * pShard: shard data
 * return: >0 if rebuilding is done, repaired data in the buffer provided at beginning of rebuilding process, automatically free handle, 0 if more shards required, <0 if something wrong
 */
 short LRC_OneShardForRebuild(short handle, const void *pShard);

/*
 * Abandon a decode or rebuild process
 */
void LRC_FreeHandle(short handle);


#endif  // YTLRC_H
