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

#include "cm256.h"


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

extern "C" int cm256_init_(int version)
{
    if (version != CM256_VERSION)
    {
        // User's header does not match library version
        return -10;
    }

    // Return error code from GF(256) init if required
    return gf256_init();
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

// This function generates each matrix element based on x_i, x_0, y_j
// Note that for x_i == x_0, this will return 1, so it is better to unroll out the first row.
static GF256_FORCE_INLINE unsigned char GetMatrixElement(unsigned char iMatrix, unsigned char matrixLen, unsigned char iElement)
{
    assert(iMatrix >= matrixLen);
#ifdef TANGCODE
    if ( matrixLen == 128 && iMatrix < matrixLen + GF256Ctx.Max128RecoveryMatrixes )
        return GF256Ctx.GF256_128[iMatrix-matrixLen][iElement];
#endif
    return gf256_div(gf256_add(iElement, matrixLen), gf256_add(iMatrix, iElement));
}


//-----------------------------------------------------------------------------
// Encoding

extern "C" void CM256EncodeBlock(
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

        memcpy(recoveryBlockData, originals[0].pData, params.BlockBytes);
        return;
    }
    // else OriginalCount >= 2:

#ifndef TANGCODE

    // Unroll first row of recovery matrix:
    // The matrix we generate for the first row is all ones,
    // so it is merely a parity of the original data.
    if (recoveryBlockIndex == params.TotalOriginalCount)
    {
        int y = params.FirstElement + params.Step;
        gf256_addset_mem(recoveryBlockData, originals[params.FirstElement].pData, originals[y].pData, params.BlockBytes);
        for (int j = 2; j < params.OriginalCount; ++j)
        {
            y += params.Step;
            gf256_add_mem(recoveryBlockData, originals[y].pData, params.BlockBytes);
        }
        return;
    }
#endif  // !TANGCODE

    // TBD: Faster algorithms seem to exist for computing this matrix-vector product.

    // Start the x_0 values arbitrarily from the original count.
    const uint8_t x_0 = static_cast<uint8_t>(params.TotalOriginalCount);

    // For other rows:
    {
        const uint8_t x_i = static_cast<uint8_t>(recoveryBlockIndex);

        // Unroll first operation for speed
        {
            const uint8_t y_0 = params.FirstElement;
            const uint8_t matrixElement = GetMatrixElement(x_i, x_0, y_0);

            gf256_mul_mem(recoveryBlockData, originals[y_0].pData, matrixElement, params.BlockBytes);
        }

        // For each original data column,
        for (int j = 1; j < params.OriginalCount; ++j)
        {
            const uint8_t y_j = static_cast<uint8_t>(params.FirstElement + j * params.Step);
            const uint8_t matrixElement = GetMatrixElement(x_i, x_0, y_j);

            gf256_muladd_mem(recoveryBlockData, matrixElement, originals[y_j].pData, params.BlockBytes);
        }
    }
}

extern "C" int cm256_encode(
    CM256LRC paramLRC, // LRC Encoder params
    CM256Block* originals,      // Array of pointers to original blocks
    uint8_t* recoveryData)        // Output recovery blocks end-to-end
{
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

    cm256_encoder_params_t params;
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
    for (int i = 0; i < paramLRC.VerLocalCount; i++) {
        CM256EncodeBlock(params, originals, params.TotalOriginalCount, pRecoveryData);
        pRecoveryData += params.BlockBytes;
        params.FirstElement += paramLRC.HorLocalCount;
    }

    /*
     * Calculate vertical recovery blocks
     */
    params.OriginalCount = paramLRC.VerLocalCount;
    params.Step = paramLRC.HorLocalCount;
    for (int i = 0; i < paramLRC.HorLocalCount; i++) {
        params.FirstElement = i;
        CM256EncodeBlock(params, originals, params.TotalOriginalCount+1, pRecoveryData);
        pRecoveryData += params.BlockBytes;
        
    }

    /*
     * Calculate global recovery blocks
     */
    params.OriginalCount = paramLRC.OriginalCount;
    params.RecoveryCount = paramLRC.GlobalRecoveryCount;
    params.FirstElement = 0;
    params.Step = 1;
    CM256Block globalRecoveryBlocks[256];
    for (int i = 0; i < paramLRC.GlobalRecoveryCount; i++)
    {
        /*
         * First recovery matrix is used for horizon recovery, 2nd matrix is used for vertical recovery, 
         * so global recovery start from 2
         */
        globalRecoveryBlocks[i].pData = pRecoveryData;
        CM256EncodeBlock(params, originals, (params.TotalOriginalCount + i + 2), pRecoveryData);
        pRecoveryData += params.BlockBytes;
    }

#ifdef NOT_USE    
    /* Calculate local recovery block for global recovery blocks */
    params.TotalOriginalCount = paramLRC.GlobalRecoveryCount;
    params.OriginalCount = paramLRC.GlobalRecoveryCount;
    params.RecoveryCount = 1;
    params.FirstElement = 0;
    params.Step = 1;
    CM256EncodeBlock(params, globalRecoveryBlocks, params.TotalOriginalCount, pRecoveryData);
#endif

    return 0;
}


//-----------------------------------------------------------------------------
// Decoding

struct CM256Decoder
{
    // Encode parameters
    cm256_encoder_params Params;

    // Recovery blocks
    CM256Block* recoveryBlock[256];
    int RecoveryCount;

    // Original blocks
    CM256Block* originalBlock[256];
    int OriginalCount;

    // Row indices that were erased
    uint8_t ErasuresIndices[256];

    // Initialize the decoder
    bool Initialize(cm256_encoder_params& params, CM256Block* blocks);

    // Decode m=1 case
    void DecodeM1();

    // Decode for m>1 case
    void Decode();

    // Generate the LU decomposition of the matrix
    void GenerateLDUDecomposition(uint8_t* matrix_L, uint8_t* diag_D, uint8_t* matrix_U);
};

bool CM256Decoder::Initialize(cm256_encoder_params& params, CM256Block* blocks)
{
    Params = params;

    CM256Block* block = blocks + params.FirstElement;
    OriginalCount = 0;
    RecoveryCount = 0;

    // Initialize original flag
    bool bOriginal[256];
    for (int ii = 0; ii < SIZEOF(bOriginal); ++ii) {
        bOriginal[ii] = false;
        ErasuresIndices[ii] = 0;
    }

    // For each input block,
    for (int ii = 0; ii < params.OriginalCount; ++ii)    {
        int lrcIndex = block->lrcIndex;

        // If it is an original block,
        if (lrcIndex < params.TotalOriginalCount)
        {
            originalBlock[OriginalCount++] = block;

            if ( bOriginal[lrcIndex] )
            {
                // Error out if two row indices repeat
                return false;
            }

            bOriginal[lrcIndex] = true;
        } else {
            recoveryBlock[RecoveryCount++] = block;
        }
        block += params.Step;
    }

    // Identify erasures
    for (int ii = Params.FirstElement, indexCount = 0; indexCount < RecoveryCount && ii < 256; ii += Params.Step) {
        if ( !bOriginal[ii] )
            ErasuresIndices[indexCount++] = static_cast<uint8_t>( ii );
    }

    return true;
}

void CM256Decoder::DecodeM1()
{
    // XOR all other blocks into the recovery block
    uint8_t* outBlock = recoveryBlock[0]->pData;
    const uint8_t* inBlock = nullptr;

    // For each block,
    for (int ii = 0; ii < OriginalCount; ++ii)
    {
        const uint8_t* inBlock2 = originalBlock[ii]->pData;

        if (NULL == inBlock)
        {
            inBlock = inBlock2;
        }
        else
        {
            // outBlock ^= inBlock ^ inBlock2
            gf256_add2_mem(outBlock, inBlock, inBlock2, Params.BlockBytes);
            inBlock = nullptr;
        }
    }

    // Complete XORs
    if (inBlock)
    {
        gf256_add_mem(outBlock, inBlock, Params.BlockBytes);
    }

    // Recover the index it corresponds to
    recoveryBlock[0]->lrcIndex = ErasuresIndices[0];
}

// Generate the LU decomposition of the matrix
void CM256Decoder::GenerateLDUDecomposition(uint8_t* matrix_L, uint8_t* diag_D, uint8_t* matrix_U)
{
    // Schur-type-direct-Cauchy algorithm 2.5 from
    // "Pivoting and Backward Stability of Fast Algorithms for Solving Cauchy Linear Equations"
    // T. Boros, T. Kailath, V. Olshevsky
    // Modified for practical use.  I folded the diagonal parts of U/L matrices into the
    // diagonal one to reduce the number of multiplications to perform against the input data,
    // and organized the triangle matrices in memory to allow for faster SSE3 GF multiplications.

    // Matrix size NxN
    const int N = RecoveryCount;

    // Generators
    uint8_t g[256], b[256];
    for (int i = 0; i < N; ++i)
    {
        g[i] = 1;
        b[i] = 1;
    }

    // Temporary buffer for rotated row of U matrix
    // This allows for faster GF bulk multiplication
    uint8_t rotated_row_U[256];
    uint8_t* last_U = matrix_U + ((N - 1) * N) / 2 - 1;
    int firstOffset_U = 0;

    // Start the x_0 values arbitrarily from the original count.
    const uint8_t x_0 = static_cast<uint8_t>(Params.TotalOriginalCount);

    // Unrolling k = 0 just makes it slower for some reason.
    for (int k = 0; k < N - 1; ++k)
    {
        const uint8_t x_k = recoveryBlock[k]->decodeIndex;
        const uint8_t y_k = ErasuresIndices[k];

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
        for (int j = k + 1; j < N; ++j)
        {
            const uint8_t x_j = recoveryBlock[j]->decodeIndex; 
            const uint8_t y_j = ErasuresIndices[j];

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
        for (int j = k + 1; j < N; ++j)
        {
            *output_U = *row_U++;
            output_U -= j;
        }
        firstOffset_U -= k + 2;
    }

    // Multiply diagonal matrix into U
    uint8_t* row_U = matrix_U;
    for (int j = N - 1; j > 0; --j)
    {
        const uint8_t y_j = ErasuresIndices[j];
        const int count = j;

        gf256_mul_mem(row_U, row_U, gf256_add(x_0, y_j), count);
        row_U += count;
    }

    const uint8_t x_n = recoveryBlock[N - 1]->decodeIndex;
    const uint8_t y_n = ErasuresIndices[N - 1];

    // D_nn = 1 / (x_n + y_n)
    // L_nn = g[N-1]
    // U_nn = b[N-1] * (x_0 + y_n)
    const uint8_t L_nn = g[N - 1];
    const uint8_t U_nn = gf256_mul(b[N - 1], gf256_add(x_0, y_n));

    // diag_D[N-1] = L_nn * D_nn * U_nn
    diag_D[N - 1] = gf256_div(gf256_mul(L_nn, U_nn), gf256_add(x_n, y_n));
}

void CM256Decoder::Decode()
{
    // Matrix size is NxN, where N is the number of recovery blocks used.
    const int N = RecoveryCount;

    // Eliminate original data from the the recovery rows
    for (int originalIndex = 0; originalIndex < OriginalCount; ++originalIndex)
    {
        const uint8_t* inBlock = originalBlock[originalIndex]->pData;
        const uint8_t iElement = originalBlock[originalIndex]->lrcIndex;

        for (int recoveryIndex = 0; recoveryIndex < N; ++recoveryIndex)
        {
            uint8_t* outBlock = recoveryBlock[recoveryIndex]->pData;
            const uint8_t matrixElement = GetMatrixElement(recoveryBlock[recoveryIndex]->decodeIndex, Params.TotalOriginalCount, iElement);

            gf256_muladd_mem(outBlock, matrixElement, inBlock, Params.BlockBytes);
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
        dynamicMatrix = new uint8_t[requiredSpace];
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
    GenerateLDUDecomposition(matrix_L, diag_D, matrix_U);

    /*
        Eliminate lower left triangle.
    */
    // For each column,
    for (int j = 0; j < N - 1; ++j)
    {
        const void* block_j = recoveryBlock[j]->pData;

        // For each row,
        for (int i = j + 1; i < N; ++i)
        {
            void* block_i = recoveryBlock[i]->pData;
            const uint8_t c_ij = *matrix_L++; // Matrix elements are stored column-first, top-down.

            gf256_muladd_mem(block_i, c_ij, block_j, Params.BlockBytes);
        }
    }

    /*
        Eliminate diagonal.
    */
    for (int i = 0; i < N; ++i)
    {
        uint8_t* blockData = recoveryBlock[i]->pData;

        recoveryBlock[i]->lrcIndex = ErasuresIndices[i];
        recoveryBlock[i]->decodeIndex = 0; //recoveryBlock[i]->lrcIndex;

        gf256_div_mem(blockData, blockData, diag_D[i], Params.BlockBytes);
    }

    /*
        Eliminate upper right triangle.
    */
    for (int j = N - 1; j >= 1; --j)
    {
        const void* block_j = recoveryBlock[j]->pData;

        for (int i = j - 1; i >= 0; --i)
        {
            void* block_i = recoveryBlock[i]->pData;
            const uint8_t c_ij = *matrix_U++; // Matrix elements are stored column-first, bottom-up.

            gf256_muladd_mem(block_i, c_ij, block_j, Params.BlockBytes);
        }
    }

    delete[] dynamicMatrix;
}

extern "C" int cm256_decode(
    cm256_encoder_params params, // Encoder params
    CM256Block* blocks)         // Array of 'originalCount' blocks as described above
{
    if (params.OriginalCount <= 0 || params.RecoveryCount <= 0 || params.TotalOriginalCount < params.OriginalCount || params.BlockBytes <= 0 || params.FirstElement < 0 || params.FirstElement > params.TotalOriginalCount || params.Step <= 0)
    {
        return -1;
    }
    if (params.TotalOriginalCount + params.RecoveryCount > 256)
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
    if (!state.Initialize(params, blocks))
    {
        return -5;
    }

    // If nothing is erased,
    if (state.RecoveryCount <= 0)
    {
        return 0;
    }

#ifdef NOT_USE
    // If m=1,
    if (params.RecoveryCount == 1)
    {
        state.DecodeM1();
        return 0;
    }
#endif

    // Decode for m>1
    state.Decode();
    return 0;
}
