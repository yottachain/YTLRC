# YottaChain Locally Repairable Code
Based on CM256, a Fast GF(256) Cauchy MDS Block Erasure Codec in C

YTLRC is a library for LRC erasure codes.  From given data it generates
redundant data that can be used to recover the originals. It can recover
 missing chunk(s) by a small number of chunks, other than requiring almost
 all of other chunks. For example, a 128 + 36 YTLRC codes can recover a
 missing chunk by 8 chunks, instead of 128 chunks. This feature can improve
 storage rebuilding performance very much.

It is roughly 2x faster than Longhair, and supports input data that is not a multiple of 8 bytes.

Currently only Visual Studio 2013 is supported, though other versions of MSVC may work.

The original data should be split up into equally-sized chunks.  If one of these chunks
is erased, the redundant data can fill in the gap through decoding.


This API was designed to be flexible enough for UDP/IP-based file transfer where
the blocks arrive out of order.


#### Credits

This software was written by Christopher A. Taylor <mrcatid@gmail.com> ) as a fast implementation of reed-solomon code
for GF(256), please refer https://github.com/catid/cm256.
Alex Wang adds LRC feature under help of TANG Xiaohu <xhutang@swjtu.edu.cn> who is a professor of Southwest Jiaotong University, China.
