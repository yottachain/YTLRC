/*
	Copyright (c) 2019 YottaChain Foundation Ltd. 2015 Christopher A. Taylor.  All rights reserved.

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	* Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
	* Neither the name of CM256/YTLRC nor the names of its contributors may be
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
#include "cm256.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>



/*
    GF(256) Cauchy Matrix Overview

    As described on Wikipedia, each element of a normal Cauchy matrix is defined as:

        a_ij = 1 / (x_i - y_j)
        The arrays x_i and y_j are vector parameters of the matrix.
        The values in x_i cannot be reused in y_j.

    Moving beyond the Wikipedia...

    (1) Number of rows (R) is the range of i, and number of columns (C) is the range of j.

    (2) Being able to select x_i and y_j makes Cauchy matrices more flexible in practice
        than Vandermonde matrices, which only have one parameter per row.

    (3) Cauchy matrices are always invertible, AKA always full rank, AKA when treated as
        as linear system y = M*x, the linear system has a single solution.

    (4) A Cauchy matrix concatenated below a square CxC identity matrix always has rank C,
        Meaning that any R rows can be eliminated from the concatenated matrix and the
        matrix will still be invertible.  This is how Reed-Solomon erasure codes work.

    (5) Any row or column can be multiplied by non-zero values, and the resulting matrix
        is still full rank.  This is true for any matrix, since it is effectively the same
        as pre and post multiplying by diagonal matrices, which are always invertible.

    (6) Matrix elements with a value of 1 are much faster to operate on than other values.
        For instance a matrix of [1, 1, 1, 1, 1] is invertible and much faster for various
        purposes than [2, 2, 2, 2, 2].

    (7) For GF(256) matrices, the symbols in x_i and y_j are selected from the numbers
        0...255, and so the number of rows + number of columns may not exceed 256.
        Note that values in x_i and y_j may not be reused as stated above.

    In summary, Cauchy matrices
        are preferred over Vandermonde matrices.  (2)
        are great for MDS erasure codes.  (3) and (4)
        should be optimized to include more 1 elements.  (5) and (6)
        have a limited size in GF(256), rows+cols <= 256.  (7)
*/


//-----------------------------------------------------------------------------
// Initialization
extern int cm256_init_(int version)
{
    if (version != CM256_VERSION)
    {
        // User's header does not match library version
        return -10;
    }

    // Return error code from GF(256) init if required
    return gf256_init();
}

static int WriteAddrToFile(void *addr, char *entry, char *filename)
{
    int fd;
	char addrstr[10];
	char des[512];
	//char filelog[] = "/root/c_malloclogcm256";
	//filename = filelog;
	unsigned long  addrint = (unsigned long)addr;
	//ultoa(addrint,addrstr,10);
	sprintf(addrstr,"%lu",addrint);
	strcpy(des,entry);
	strcat(des," ");
	strcat(des,addrstr);
	strcat(des,"\n\r");
	fd = open(filename,O_RDWR|O_CREAT|O_APPEND);	
	write(fd,des,sizeof(des));
	close(fd);
	sleep(2);
	return 0;
}


/*
    Selected Cauchy Matrix Form

    The matrix consists of elements a_ij, where i = row, j = column.
    a_ij = 1 / (x_i - y_j), where x_i and y_j are sets of GF(256) values
    that do not intersect.

    We select x_i and y_j to just be incrementing numbers for the
    purposes of this library.  Further optimizations may yield matrices
    with more 1 elements, but the benefit seems relatively small.

    The x_i values range from 0...(originalCount - 1).
    The y_j values range from originalCount...(originalCount + recoveryCount - 1).

    We then improve the Cauchy matrix by dividing each column by the
    first row element of that column.  The result is an invertible
    matrix that has all 1 elements in the first row.  This is equivalent
    to a rotated Vandermonde matrix, so we could have used one of those.

    The advantage of doing this is that operations involving the first
    row will be extremely fast (just memory XOR), so the decoder can
    be optimized to take advantage of the shortcut when the first
    recovery row can be used.

    First row element of Cauchy matrix for each column:
    a_0j = 1 / (x_0 - y_j) = 1 / (x_0 - y_j)

    Our Cauchy matrix sets first row to ones, so:
    a_ij = (1 / (x_i - y_j)) / a_0j
    a_ij = (y_j - x_0) / (x_i - y_j)
    a_ij = (y_j + x_0) div (x_i + y_j) in GF(256)
*/


//-----------------------------------------------------------------------------
// Encoding

extern void CM256EncodeBlock(
    cm256_encoder_params params, // Encoder parameters
    CM256Block* originals,      // Array of pointers to original blocks
    int recoveryBlockIndex,      // Return value from cm256_get_recovery_block_index()
    uint8_t* recoveryBlockData)         // Output recovery block data
{
    assert(params.TotalOriginalCount < 256 && params.FirstElement + (params.OriginalCount-1) * params.Step < params.TotalOriginalCount);

    // If only one block of input data,
    if (params.OriginalCount == 1)
    {
        // No meaningful operation here, degenerate to outputting the same data each time.

        memcpy(recoveryBlockData, originals[0].pData, params.BlockBytes);\
        return;
    }
    // else OriginalCount >= 2:

#ifndef TANGCODE

    // Unroll first row of recovery matrix:
    // The matrix we generate for the first row is all ones,
    // so it is merely a parity of the original data.
    if (recoveryBlockIndex == params.TotalOriginalCount)
    {
        int j;
        int y = params.FirstElement + params.Step;
        gf256_addset_mem(recoveryBlockData, originals[params.FirstElement].pData, originals[y].pData, params.BlockBytes);
        for (j = 2; j < params.OriginalCount; j++)
        {
            y += params.Step;
            gf256_add_mem(recoveryBlockData, originals[y].pData, params.BlockBytes);
        }
        return;
    }
#endif  // !TANGCODE

    // TBD: Faster algorithms seem to exist for computing this matrix-vector product.

    // Start the x_0 values arbitrarily from the original count.
    const uint8_t x_0 = (uint8_t)(params.TotalOriginalCount);

    // For other rows:
    {
        int j;
        const uint8_t x_i = (uint8_t)(recoveryBlockIndex);

        // Unroll first operation for speed
        {
            const uint8_t y_0 = params.FirstElement;
            const uint8_t matrixElement = GetMatrixElement(x_i, x_0, y_0);

            gf256_mul_mem(recoveryBlockData, originals[y_0].pData, matrixElement, params.BlockBytes);
        }

        // For each original data column,
        for (j = 1; j < params.OriginalCount; j++) {
            const uint8_t y_j = (uint8_t)(params.FirstElement + j * params.Step);
            const uint8_t matrixElement = GetMatrixElement(x_i, x_0, y_j);

            gf256_muladd_mem(recoveryBlockData, matrixElement, originals[y_j].pData, params.BlockBytes);
        }
    }
}

extern int cm256_encode(
    CM256LRC paramLRC, // LRC Encoder params
    CM256Block* originals,      // Array of pointers to original blocks
    uint8_t* recoveryData)        // Output recovery blocks end-to-end
{
    short i;
    // Validate input:
    if (paramLRC.OriginalCount <= 0 ||
        paramLRC.TotalRecoveryCount <= 3 ||
        paramLRC.BlockBytes <= 0)
    {
        return -1;
    }
    if (paramLRC.TotalOriginalCount + paramLRC.TotalRecoveryCount > 256)
    {
        return -2;
    }
    if (NULL == originals || NULL == recoveryData)
    {
        return -3;
    }

    cm256_encoder_params params;
    params.TotalOriginalCount = paramLRC.TotalOriginalCount;
    params.BlockBytes = paramLRC.BlockBytes;
    /*
     * Calculate horizon recovery blocks
     */
    params.OriginalCount = paramLRC.HorLocalCount;
    params.RecoveryCount = 1;
    params.FirstElement = 0;
    params.Step = 1;
    uint8_t* pRecoveryData = recoveryData;
    for (i = 0; i < paramLRC.VerLocalCount; i++) {
        if ( paramLRC.bIndexByte )
            *pRecoveryData++ = paramLRC.OriginalCount + i;
        CM256EncodeBlock(params, originals, params.TotalOriginalCount, pRecoveryData);
        pRecoveryData += params.BlockBytes;
        params.FirstElement += paramLRC.HorLocalCount;
    }

    /*
     * Calculate vertical recovery blocks
     */
    params.OriginalCount = paramLRC.VerLocalCount;
    params.Step = paramLRC.HorLocalCount;
    for (i = 0; i < paramLRC.HorLocalCount; i++) {
        if ( paramLRC.bIndexByte )
            *pRecoveryData++ = paramLRC.OriginalCount + paramLRC.VerLocalCount + i;
        params.FirstElement = i;
        CM256EncodeBlock(params, originals, params.TotalOriginalCount+1, pRecoveryData);
        pRecoveryData += params.BlockBytes;        
    }

    /*
     * Calculate global recovery blocks
     */
    uint8_t *pLocalGlobalRecoveryData;
    if ( !paramLRC.bIndexByte )
        pLocalGlobalRecoveryData = pRecoveryData + paramLRC.GlobalRecoveryCount * params.BlockBytes;
    else {
        pLocalGlobalRecoveryData = pRecoveryData + paramLRC.GlobalRecoveryCount * (params.BlockBytes + 1);
        *pLocalGlobalRecoveryData++ = paramLRC.OriginalCount + paramLRC.LocalRecoveryOfGlobalRecoveryIndex;;
    }
    memset(pLocalGlobalRecoveryData, 0, params.BlockBytes);
    params.OriginalCount = paramLRC.OriginalCount;
    params.RecoveryCount = paramLRC.GlobalRecoveryCount;
    params.FirstElement = 0;
    params.Step = 1;
    for (i = 0; i < paramLRC.GlobalRecoveryCount; i++) {
        /*
         * First recovery matrix is used for horizon recovery, 2nd matrix is used for vertical recovery, 
         * so global recovery start from 2
         */
        if ( paramLRC.bIndexByte )
            *pRecoveryData++ = paramLRC.OriginalCount + paramLRC.VerLocalCount + paramLRC.HorLocalCount + i;
        CM256EncodeBlock(params, originals, (params.TotalOriginalCount + i + 2), pRecoveryData);

        gf256_add_mem(pLocalGlobalRecoveryData, pRecoveryData, params.BlockBytes);  // Figure local recovery block of global recovery data
        //for (j = 0; j < params.BlockBytes; j++)
        //    pLocalGlobalRecoveryData[j] ^= pRecoveryData[j];

        pRecoveryData += params.BlockBytes;
    }
    
    return 0;
}


//-----------------------------------------------------------------------------
// Decoding

extern bool DecoderInitialize(CM256Decoder *pDecoder, const cm256_encoder_params *pParams, CM256Block* blocks)
{
    int ii;
    pDecoder->Params = *pParams;

    CM256Block* block = blocks + pParams->FirstElement;
    pDecoder->OriginalCount = 0;
    pDecoder->RecoveryCount = 0;

    // Initialize original flag
    bool bOriginal[MAXSHARDS];
    for (ii = 0; ii < MAXSHARDS; ii++) {
        bOriginal[ii] = false;
        pDecoder->ErasuresIndices[ii] = 0;
    }

    // For each input block,
    for (ii = 0; ii < pParams->OriginalCount; ii++) {
        int lrcIndex = block->lrcIndex;

        // If it is an original block,
        if (lrcIndex < pParams->TotalOriginalCount)
        {
            pDecoder->originalBlock[pDecoder->OriginalCount++] = block;

            if ( bOriginal[lrcIndex] )
            {
                // Error out if two row indices repeat
                return false;
            }

            bOriginal[lrcIndex] = true;
        } else {
            pDecoder->recoveryBlock[pDecoder->RecoveryCount++] = block;
        }
        block += pParams->Step;
    }

    // Identify erasures
    int indexCount;
    for (ii = pParams->FirstElement, indexCount = 0; indexCount < pDecoder->RecoveryCount && ii < MAXSHARDS; ii += pParams->Step) {
        if ( !bOriginal[ii] )
            pDecoder->ErasuresIndices[indexCount++] = (uint8_t)( ii );
    }

    return true;
}

extern void DecodeM1(CM256Decoder *pDecoder)
{
    int ii;
    // XOR all other blocks into the recovery block
    uint8_t* outBlock = pDecoder->recoveryBlock[0]->pData;
    size_t blockBytes = pDecoder->Params.BlockBytes;
#ifdef NOT_USE
    for (ii = 0; ii < pDecoder->OriginalCount; ii++)
        gf256_add_mem(outBlock, pDecoder->originalBlock[ii]->pData, blockBytes);
#endif
    const uint8_t* inBlock = nullptr;

    // For each block,
    for (ii = 0; ii < pDecoder->OriginalCount; ii++) {
        const uint8_t* inBlock2 = pDecoder->originalBlock[ii]->pData;

        if (nullptr == inBlock)
            inBlock = inBlock2;
        else {
            // outBlock ^= inBlock ^ inBlock2
            gf256_add2_mem(outBlock, inBlock, inBlock2, blockBytes);
            inBlock = nullptr;
        }
    }

    // Complete XORs
    if (nullptr != inBlock)
    {
        gf256_add_mem(outBlock, inBlock, pDecoder->Params.BlockBytes);
    }

    // Recover the index it corresponds to
    pDecoder->recoveryBlock[0]->decodeIndex = pDecoder->recoveryBlock[0]->lrcIndex = pDecoder->ErasuresIndices[0];
}

// Generate the LU decomposition of the matrix
extern void GenerateLDUDecomposition(CM256Decoder *pDecoder, uint8_t* matrix_L, uint8_t* diag_D, uint8_t* matrix_U)
{
    // Schur-type-direct-Cauchy algorithm 2.5 from
    // "Pivoting and Backward Stability of Fast Algorithms for Solving Cauchy Linear Equations"
    // T. Boros, T. Kailath, V. Olshevsky
    // Modified for practical use.  I folded the diagonal parts of U/L matrices into the
    // diagonal one to reduce the number of multiplications to perform against the input data,
    // and organized the triangle matrices in memory to allow for faster SSE3 GF multiplications.

    int i, j, k;
    // Matrix size NxN
    const int N = pDecoder->RecoveryCount;

    // Generators
    uint8_t g[MAXSHARDS], b[MAXSHARDS];
    for (i = 0; i < N; ++i) {
        g[i] = 1;
        b[i] = 1;
    }

    // Temporary buffer for rotated row of U matrix
    // This allows for faster GF bulk multiplication
    uint8_t rotated_row_U[MAXSHARDS];
    uint8_t* last_U = matrix_U + ((N - 1) * N) / 2 - 1;
    int firstOffset_U = 0;

    // Start the x_0 values arbitrarily from the original count.
    const uint8_t x_0 = (uint8_t)(pDecoder->Params.TotalOriginalCount);

    // Unrolling k = 0 just makes it slower for some reason.
    for (k = 0; k < N - 1; ++k)
    {
        const uint8_t x_k = pDecoder->recoveryBlock[k]->decodeIndex;
        const uint8_t y_k = pDecoder->ErasuresIndices[k];

        // D_kk = (x_k + y_k)
        // L_kk = g[k] / (x_k + y_k)
        // U_kk = b[k] * (x_0 + y_k) / (x_k + y_k)
        const uint8_t D_kk = gf256_add(x_k, y_k);
        const uint8_t L_kk = gf256_div(g[k], D_kk);
        const uint8_t U_kk = gf256_mul(gf256_div(b[k], D_kk), gf256_add(x_0, y_k));

        // diag_D[k] = D_kk * L_kk * U_kk
        diag_D[k] = gf256_mul(D_kk, gf256_mul(L_kk, U_kk));

        // Computing the k-th row of L and U
        uint8_t* row_L = matrix_L;
        uint8_t* row_U = rotated_row_U;
        for (j = k + 1; j < N; ++j)
        {
            const uint8_t x_j = pDecoder->recoveryBlock[j]->decodeIndex; 
            const uint8_t y_j = pDecoder->ErasuresIndices[j];

            // L_jk = g[j] / (x_j + y_k)
            // U_kj = b[j] / (x_k + y_j)
            const uint8_t L_jk = gf256_div(g[j], gf256_add(x_j, y_k));
            const uint8_t U_kj = gf256_div(b[j], gf256_add(x_k, y_j));

            *matrix_L++ = L_jk;
            *row_U++ = U_kj;

            // g[j] = g[j] * (x_j + x_k) / (x_j + y_k)
            // b[j] = b[j] * (y_j + y_k) / (y_j + x_k)
            g[j] = gf256_mul(g[j], gf256_div(gf256_add(x_j, x_k), gf256_add(x_j, y_k)));
            b[j] = gf256_mul(b[j], gf256_div(gf256_add(y_j, y_k), gf256_add(y_j, x_k)));
        }

        // Do these row/column divisions in bulk for speed.
        // L_jk /= L_kk
        // U_kj /= U_kk
        const int count = N - (k + 1);
        gf256_div_mem(row_L, row_L, L_kk, count);
        gf256_div_mem(rotated_row_U, rotated_row_U, U_kk, count);

        // Copy U matrix row into place in memory.
        uint8_t* output_U = last_U + firstOffset_U;
        row_U = rotated_row_U;
        for (j = k + 1; j < N; ++j)
        {
            *output_U = *row_U++;
            output_U -= j;
        }
        firstOffset_U -= k + 2;
    }

    // Multiply diagonal matrix into U
    uint8_t* row_U = matrix_U;
    for (j = N - 1; j > 0; --j)
    {
        const uint8_t y_j = pDecoder->ErasuresIndices[j];
        const int count = j;

        gf256_mul_mem(row_U, row_U, gf256_add(x_0, y_j), count);
        row_U += count;
    }

    const uint8_t x_n = pDecoder->recoveryBlock[N - 1]->decodeIndex;
    const uint8_t y_n = pDecoder->ErasuresIndices[N - 1];

    // D_nn = 1 / (x_n + y_n)
    // L_nn = g[N-1]
    // U_nn = b[N-1] * (x_0 + y_n)
    const uint8_t L_nn = g[N - 1];
    const uint8_t U_nn = gf256_mul(b[N - 1], gf256_add(x_0, y_n));

    // diag_D[N-1] = L_nn * D_nn * U_nn
    diag_D[N - 1] = gf256_div(gf256_mul(L_nn, U_nn), gf256_add(x_n, y_n));
}

extern void Decode(CM256Decoder *pDecoder)
{
    int originalIndex, recoveryIndex, i, j;
    // Matrix size is NxN, where N is the number of recovery blocks used.
    const int N = pDecoder->RecoveryCount;

    // Eliminate original data from the the recovery rows
    for (originalIndex = 0; originalIndex < pDecoder->OriginalCount; ++originalIndex)
    {
        const uint8_t* inBlock = pDecoder->originalBlock[originalIndex]->pData;
        const uint8_t iElement = pDecoder->originalBlock[originalIndex]->lrcIndex;

        for (recoveryIndex = 0; recoveryIndex < N; ++recoveryIndex)
        {
            uint8_t* outBlock = pDecoder->recoveryBlock[recoveryIndex]->pData;
            const uint8_t matrixElement = GetMatrixElement(pDecoder->recoveryBlock[recoveryIndex]->decodeIndex, pDecoder->Params.TotalOriginalCount, iElement);

            gf256_muladd_mem(outBlock, matrixElement, inBlock, pDecoder->Params.BlockBytes);
        }
    }

    // Allocate matrix
    static const int StackAllocSize = 2048;
    uint8_t stackMatrix[StackAllocSize];
    uint8_t* dynamicMatrix = nullptr;
    uint8_t* matrix = stackMatrix;
    const int requiredSpace = N * N;
    if (requiredSpace > StackAllocSize)
    {
        dynamicMatrix = (uint8_t*)malloc(requiredSpace);
		WriteAddrToFile(dynamicMatrix,"dynamicMatrix","/root/c_malloc");
        matrix = dynamicMatrix;
    }

    /*
        Compute matrix decomposition:

            G = L * D * U

        L is lower-triangular, diagonal is all ones.
        D is a diagonal matrix.
        U is upper-triangular, diagonal is all ones.
    */
    uint8_t* matrix_U = matrix;
    uint8_t* diag_D = matrix_U + (N - 1) * N / 2;
    uint8_t* matrix_L = diag_D + N;
    GenerateLDUDecomposition(pDecoder, matrix_L, diag_D, matrix_U);

    /*
        Eliminate lower left triangle.
    */
    // For each column,
    for (j = 0; j < N - 1; ++j)
    {
        const void* block_j = pDecoder->recoveryBlock[j]->pData;

        // For each row,
        for (i = j + 1; i < N; ++i)
        {
            void* block_i = pDecoder->recoveryBlock[i]->pData;
            const uint8_t c_ij = *matrix_L++; // Matrix elements are stored column-first, top-down.

            gf256_muladd_mem(block_i, c_ij, block_j, pDecoder->Params.BlockBytes);
        }
    }

    /*
        Eliminate diagonal.
    */
    for (i = 0; i < N; ++i)
    {
        uint8_t* blockData = pDecoder->recoveryBlock[i]->pData;

        pDecoder->recoveryBlock[i]->decodeIndex = pDecoder->recoveryBlock[i]->lrcIndex = pDecoder->ErasuresIndices[i];

        gf256_div_mem(blockData, blockData, diag_D[i], pDecoder->Params.BlockBytes);
    }

    /*
        Eliminate upper right triangle.
    */
    for (j = N - 1; j >= 1; --j)
    {
        const void* block_j = pDecoder->recoveryBlock[j]->pData;

        for (i = j - 1; i >= 0; --i)
        {
            void* block_i = pDecoder->recoveryBlock[i]->pData;
            const uint8_t c_ij = *matrix_U++; // Matrix elements are stored column-first, bottom-up.

            gf256_muladd_mem(block_i, c_ij, block_j, pDecoder->Params.BlockBytes);
        }
    }

    if (NULL != dynamicMatrix){
		WriteAddrToFile(dynamicMatrix, "free_dynamicMatrix", "/root/c_cmfree");
        free(dynamicMatrix);
    }
}

extern int cm256_decode(
    cm256_encoder_params params, // Encoder params
    CM256Block* blocks)         // Array of 'originalCount' blocks as described above
{
    if (params.OriginalCount <= 0 || params.RecoveryCount <= 0 || params.TotalOriginalCount < params.OriginalCount || params.BlockBytes <= 0 || params.FirstElement < 0 || params.FirstElement > params.TotalOriginalCount || params.Step <= 0)
    {
        return -1;
    }
    if (params.TotalOriginalCount + params.RecoveryCount > MAXSHARDS)
    {
        return -2;
    }
    if (NULL == blocks)
    {
        return -3;
    }

    // If there is only one block,
    if (params.OriginalCount == 1)
    {
        // It is the same block repeated
        blocks[0].lrcIndex = 0;
        return 0;
    }

    CM256Decoder state;
    if (!DecoderInitialize(&state, &params, blocks))
    {
        return -5;
    }

    // If nothing is erased,
    if (state.RecoveryCount <= 0)
    {
        return 0;
    }

    // If m=1,
    if (params.RecoveryCount == 1 && state.recoveryBlock[0]->decodeIndex == HOR_DECODE_INDEX(&params) )
    {
        DecodeM1(&state);
        return 0;
    }

    // Decode for m>1
    Decode(&state);
    return 0;
}
