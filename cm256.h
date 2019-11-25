/*
	Copyright (c) 2015 Christopher A. Taylor.  All rights reserved.

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

#ifndef CM256_H
#define CM256_H

#include "gf256.h"

#include <assert.h>

// Library version
#define CM256_VERSION 2


#ifdef __cplusplus
extern "C" {
#endif

/*
 * Verify binary compatibility with the API on startup.
 *
 * Example:
 * 	if (cm256_init()) exit(1);
 *
 * Returns 0 on success, and any other code indicates failure.
 */
extern int cm256_init_(int version);
#define cm256_init() cm256_init_(CM256_VERSION)


// Encoder parameters
typedef struct cm256_encoder_params_t {    
    int TotalOriginalCount;  // Total of original block count < 256
    int OriginalCount;  // Current original block count < 256. It is different than TotalOriginalCount for local recovery block
    int RecoveryCount;  // Recovery block count < 256
    int FirstElement;   // 0 for global recovery block
    int Step;    // horizonal LRC count for vertical recovery block, 1 for others
    
    int BlockBytes;    // Number of bytes per block (all blocks are the same size in bytes)
} cm256_encoder_params;

// Descriptor for data block
struct CM256Block {
    // Pointer to data received.
    uint8_t* pData;

    // Block index.
    // For original data, it will be in the range
    //    [0..(originalCount-1)] inclusive.
    // For recovery data, the first one's Index must be originalCount,
    //    and it will be in the range
    //    [originalCount..(originalCount+totalRecoveryCount-1)] inclusive.
    // 
    uint8_t decodeIndex;    // The order of decoding matrix. Ignored during encoding, required during decoding.
    uint8_t lrcIndex;       // The order in LRC encoded blocks
};

struct CM256LRC {
    int OriginalCount;  // Original block count < 256
    int HorLocalCount;  // Number of blocks in a horizon local group
    int VerLocalCount;  // Number of blocks in a vertical local group. VerLocalCount = (OriginalCount + HorLocalCount -1 ) / HorLocalCount
    int TotalOriginalCount; // TotalOriginalCount = HorLocalCount * VerLocalCount, it is greater than OriginalCount when (OriginalCount % HorLocalCount) != 0
    int TotalRecoveryCount;  // Total recovery block count < 256, includes global and local. TotalRecoveryCount = GlobalRecoveryCount + HorLocalCount + VerLocalCount + 1
    int GlobalRecoveryCount;  // Recovery block count < 256
    int FirstHorRecoveryIndex;  // First one of horizon recovery blocks = 0, there are total of VerLocalCount of horizon recovery blocks
    int FirstVerRecoveryIndex;  // First one of vertical recovery blocks = VerLocalCount, there are total of HorLocalCount of vertical recovery blocks
    int FirstGlobalRecoveryIndex;  // First one of globall recovery blocks = VerLocalCount+HorLocalCount
    int LocalRecoveryOfGlobalRecoveryIndex;  // The index of local recovery block of globall recovery blocks = VerLocalCount+HorLocalCount+GlobalRecoveryCount
    
    int BlockBytes;    // Number of bytes per block (all blocks are the same size in bytes)
};

// Compute the value to put in the Index member of cm256_block
static inline unsigned char cm256_get_recovery_block_index(CM256LRC &paramLRC, int recoveryBlockIndex)
{
    assert(recoveryBlockIndex >= 0 && recoveryBlockIndex < paramLRC.TotalRecoveryCount + 2);
    return (unsigned char)(paramLRC.TotalOriginalCount + recoveryBlockIndex);
}
static inline unsigned char cm256_get_original_block_index(int OriginalCount, int originalBlockIndex)
{
    assert(originalBlockIndex >= 0 && originalBlockIndex < OriginalCount);
    return (unsigned char)(originalBlockIndex);
}


/*
 * Cauchy MDS GF(256) encode
 *
 * This produces a set of recovery blocks that should be transmitted after the
 * original data blocks.
 *
 * It takes in 'originalCount' equal-sized blocks and produces 'recoveryCount'
 * equally-sized recovery blocks.
 *
 * The input 'originals' array allows more natural usage of the library.
 * The output recovery blocks are stored end-to-end in 'recoveryBlocks'.
 * 'recoveryBlocks' should have recoveryCount * blockBytes bytes available.
 *
 * Precondition: originalCount + recoveryCount <= 256
 *
 * When transmitting the data, the block index of the data should be sent,
 * and the recovery block index is also needed.  The decoder should also
 * be provided with the values of originalCount, recoveryCount and blockBytes.
 *
 * Example wire format:
 * [originalCount(1 byte)] [recoveryCount(1 byte)]
 * [blockIndex(1 byte)] [blockData(blockBytes bytes)]
 *
 * Be careful not to mix blocks from different encoders.
 *
 * It is possible to support variable-length data by including the original
 * data length at the front of each message in 2 bytes, such that when it is
 * recovered after a loss the data length is available in the block data and
 * the remaining bytes of padding can be neglected.
 *
 * Returns 0 on success, and any other code indicates failure.
 */
extern int cm256_encode(
    CM256LRC paramLRC, // Encoder parameters
    CM256Block* originals,      // Array of pointers to original blocks
    uint8_t* recoveryData);       // Output recovery blocks end-to-end

// Encode one block.
// Note: This function does not validate input, use with care.
extern void cm256_encode_block(
    cm256_encoder_params params, // Encoder parameters
    CM256Block* originals,      // Array of pointers to original blocks
    int recoveryBlockIndex,      // Return value from cm256_get_recovery_block_index()
    uint8_t* recoveryData);        // Output recovery block

/*
 * Cauchy MDS GF(256) decode
 *
 * This recovers the original data from the recovery data in the provided
 * blocks.  There should be 'originalCount' blocks in the provided array.
 * Recovery will always be possible if that many blocks are received.
 *
 * Provide the same values for 'originalCount', 'recoveryCount', and
 * 'blockBytes' used by the encoder.
 *
 * The block Index should be set to the block index of the original data,
 * as described in the cm256_block struct comments above.
 *
 * Recovery blocks will be replaced with original data and the Index
 * will be updated to indicate the original block that was recovered.
 *
 * Returns 0 on success, and any other code indicates failure.
 */
extern int cm256_decode(
    cm256_encoder_params params, // Encoder parameters
    CM256Block* blocks);        // Array of 'originalCount' blocks as described above


#ifdef __cplusplus
}
#endif


#endif // CM256_H
