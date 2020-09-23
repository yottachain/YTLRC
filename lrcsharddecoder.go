package lrcpkg
/*
 #cgo CFLAGS: -I./
 #cgo CFLAGS: -g -Wall -O0
 #cgo CFLAGS: -Wunused-variable
// #cgo CFLAGS: -Wrestrict
 #cgo CFLAGS: -std=c99
// #cgo LDFLAGS:-L ./ -lYTLRC -lcm256 -lgf256
 #cgo amd64 CFLAGS: -mssse3
 #cgo arm arm64 CFLAGS: -DLINUX_ARM=1 -DGF256_TARGET_MOBILE
 #cgo LDFLAGS: -lm
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "./YTLRC.h"
 #include "./cm256.h"
 #include "./gf256.h"

//typedef void* pVoid;
typedef unsigned int uint;

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
}DecoderLRC_t;

static void** makeArray(int size) {
   void ** ret;
   ret = (void**)malloc(sizeof(void *) * size);
   memset(ret, 0 , sizeof(void *) * size);
   return ret;
}

static void setArray(void **a, void *s, int n) {
   a[n] = s;
}

static void* goToCmem(void *dst, void *src, int index, int blksize){
	void *ret;
    ret = memcpy(dst+(index * blksize), src, blksize);
    return ret;
}

static void* cToGomem(void *dst, void *src, int index, int blksize){
	void *ret;
    ret = memcpy(dst, src+(index * blksize), blksize);
    return ret;
}

static void* getCmemData(void *dst, void *src, int size){
	void *ret;
	ret = memcpy(dst, src, size);
	return ret;
}

//static void printRcvSlic(void *start, int sliceSize, int sliceNum){
//	int i, j;
//	for (i = 0; i < sliceNum; i++){
//		if (i==10){
//			printf("\n=============%d=========================\n",i);
//			for (j = 0; j < sliceSize; j++){
//				printf("%d ",(unsigned char)(*((char *)start + i*sliceSize + j)));
//			}
//			printf("\n=============%d=========================\n",i);
//		    //	getchar();
//		}
//	}
//	printf("*********END PRINT RECOVE SLICE*********************");
//}

//static void printPoint(void **pts ,int count) {
//	int i;
//	for ( i = 0; i < count; i++){
//		printf("i(%d)=%lx\n",i, (unsigned long)(pts[i]));
//		printf("ptsptr=%lx\n", (unsigned long)(&pts[i]));
//		printf("pts=%lx\n", (unsigned long)(pts));
//
//	}
//
//	printf("C  printPoint end!!!\n");
//}

//static void cprint(){
//	printf("this is print for test!\n");
//}

*/
import "C"
import (
	"fmt"
	"io"
	"os"
	"strconv"
	"unsafe"
)
import (
//     "fmt"
     "container/list"
)


const BufferSize = 16384
const TotalOriginalCount = 128
type Shard  [BufferSize]byte
//var IndexData uint16
//var DataList[TotalShardCount] *C.char

type OriginalShards struct {
	OriginalShards []Shard
	Result         int16
}

func (s *Shardsinfo) LRCinit(n int16) int16 {
	stat := C.LRC_Initial(C.short(13))
	if stat < 0 {
		return -2
	}
	return 1
}

func WriteAddrToFile(addr uint64, entry, fileName string) error{
	//filePath := "/root/" + fileName
	filePath2 := "/root/cgo_malloc2"
	f, err := os.OpenFile(filePath2, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0666)
	if err != nil {
		return err
	}
	str_addr := strconv.FormatUint(addr,10)
	f.Write([]byte(entry))
	n, err := f.Write([]byte(str_addr))
	if err == nil && n < len([]byte(str_addr)) {
		err = io.ErrShortWrite
	}
	f.Write([]byte("\n\r"))
	if err1 := f.Close(); err == nil {
		err = err1
	}
	return err
}

func (s *Shardsinfo)GetRCHandle(sdinf *Shardsinfo) (unsafe.Pointer){
     if (sdinf == nil) {
        return nil
     }
     
/*
     sdinf.ptrData = make([]byte,16384)
     for i:=0;i < sdinf.rebuilderNum;i++{
         datalist[i]=(*C.char)(C.malloc(C.size_t(16384)))
     }
*/
     sdinf.IndexData = 0
	 sdinf.PtrData = C.malloc(C.size_t(16384))
	 //WriteAddrToFile(uint64(uintptr(sdinf.PtrData)),"PtrData","cgo_malloc")
	 sdinf.ShardSize = BufferSize
     if sdinf.PtrData == nil {
        panic("ptrData malloc failed!\n")
     }
     
     handle := C.LRC_BeginRebuild(C.ushort(sdinf.OriginalCount),C.ushort(sdinf.Lostindex),16384,(unsafe.Pointer)(sdinf.PtrData))
	 //WriteAddrToFile(uint64(uintptr(handle)),"handle","cgo_malloc")
     sdinf.Status=1
     sdinf.Handle=handle
//     fmt.Println(sdinf.Handle)    
     return handle
}

func (s *Shardsinfo)GetNeededShardList(handle unsafe.Pointer)(*list.List,int16){
     var needlist *C.uchar
     needlist = (*C.uchar)(C.malloc(C.size_t(256)))
	 //WriteAddrToFile(uint64(uintptr(unsafe.Pointer(needlist))),"needlist","cgo_malloc")
     oll := list.New()
     var ndnum C.short
     ndnum = C.LRC_NextRequestList(handle,needlist)
     //var cchar C.char
     if (int16(ndnum) >0){
        for i:=C.short(0); i < ndnum; i++ {
            oll.PushBack(int16(*(*C.uchar)(unsafe.Pointer(uintptr(unsafe.Pointer(needlist)) + uintptr(i)))))
        }
     }
	//WriteAddrToFile(uint64(uintptr(unsafe.Pointer(needlist))),"free_needlist","cgo_free")
	C.free(unsafe.Pointer(needlist))
     return oll,int16(ndnum)
}

func (s *Shardsinfo)AddShardData(handle unsafe.Pointer,sdata []byte)(int16){
     var stat C.short

     temp := (*C.char)(C.malloc(C.size_t(16384)))
     C.memcpy(unsafe.Pointer(temp),unsafe.Pointer(&sdata[0]),16384)
     s.DataList[s.IndexData] = temp
     //if s.IndexData >= 120{
		// fmt.Println("[recover] s.IndexData=",s.IndexData)
	 //}
	 //WriteAddrToFile(uint64(uintptr(unsafe.Pointer(DataList[IndexData]))),"DataList[IndexData]","cgo_malloc")
     s.IndexData++

     stat = C.LRC_OneShardForRebuild(handle,unsafe.Pointer(temp))

     return int16(stat)
}

func (s *Shardsinfo)GetRebuildData(sdinf *Shardsinfo)([]byte,int16){
     if (sdinf.PtrData == nil){
        return nil,-1
     }
     temp := make([]byte,sdinf.ShardSize)
     var j uint32
     j=0
     for i:=C.uint(0);i < C.uint(sdinf.ShardSize);i++{
         temp[j]=byte(*((*C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(sdinf.PtrData)) + uintptr(i)))))
         j++
     }
     return temp,1
}

func (s *Shardsinfo) FreeHandle() {
	for k := uint16(0); k < s.IndexData; k++ {
		C.free(unsafe.Pointer(s.DataList[k]))
		//WriteAddrToFile(uint64(uintptr(unsafe.Pointer(DataList[k]))),"free_DataList[IndexData]","cgo_free")
	}
	C.free(s.PtrData)
	//WriteAddrToFile(uint64(uintptr(unsafe.Pointer(s.PtrData))),"free_PtrData","cgo_free")
	C.LRC_FreeHandle(s.Handle)
	//WriteAddrToFile(uint64(uintptr(unsafe.Pointer(s.Handle))),"free_Handle","cgo_free")
}

//-------LRC---------//

func SaveShardtoFile(Oshard Shard,i int){
	dir := "/home/ytago/src/GOLRC/slicedir/"
	filePath := dir+strconv.Itoa(i)
	f,err:=os.OpenFile(filePath,os.O_CREATE|os.O_TRUNC|os.O_RDWR, 0666)
	defer f.Close()
	if err != nil {
		fmt.Println("open file:",filePath," error")
		return
	}
	f.Write(Oshard[:])
}

func (s *Shardsinfo) LRCEncode(OriginalShards []Shard) []Shard {
	//s.PRecoveryData = C.malloc(BufferSize * 36)
	rcvData := make([]Shard,36)
	s.PRecoveryData = unsafe.Pointer(&rcvData[0][0])
	if s.PRecoveryData == nil {
		fmt.Println("error, cannot alloc enough space!")
		return nil
	}

	count := len(OriginalShards)
	OshardArr := C.malloc(C.size_t(count) * BufferSize)

	pts := (*unsafe.Pointer)(unsafe.Pointer(C.makeArray(C.int(len(OriginalShards)))))

	for i, Oshard := range OriginalShards {
		ret := C.goToCmem(unsafe.Pointer(OshardArr), unsafe.Pointer(&Oshard[0]), C.int(i), BufferSize)
		C.setArray(pts, ret, C.int(i))
		//SaveShardtoFile(Oshard,i)
	}

	s.ShardSize = BufferSize
	s.OriginalCount = uint16(len(OriginalShards))
	res := C.LRC_Encode(pts, C.ushort(s.OriginalCount), C.ulong(s.ShardSize), (unsafe.Pointer)(s.PRecoveryData))

	if res < 0 {
		fmt.Println("LRC_Encode error, res=",res)
		return nil
	}

	//type sliceP  *Shard
	//temp := make([]sliceP, res)
	////var j uint32
	////j = 0
	//for i := C.uint(0); i < C.uint(res); i++ {
	//	temp[i] = (sliceP)(unsafe.Pointer(uintptr(unsafe.Pointer(s.PRecoveryData)) + uintptr(i*BufferSize)))
	//	copy(rcvData[i][:],(*temp[i])[:])
	//	//rcvData=append(rcvData,(*temp[j]))
	//	SaveShardtoFile((*temp[i]),int(i+128))
	//	//j++
	//}

	//C.free(s.PRecoveryData)
	C.free(unsafe.Pointer(pts))
	C.free(OshardArr)
	return rcvData
}

func (s *Shardsinfo)SaveShardtoFile2(handle unsafe.Pointer ){
	CHandle := (* C.DecoderLRC_t)(unsafe.Pointer(handle))
	DOriginalShard := CHandle.pDecodedData
	fmt.Printf("DOriginalShard=%p\n",DOriginalShard)
	var OShard Shard
	for k := 0; k < TotalOriginalCount; k++{
		C.cToGomem(unsafe.Pointer(&OShard[0]), unsafe.Pointer(DOriginalShard), C.int(k), BufferSize-1)
		SaveShardtoFile(OShard,k)
	}
}

func (s *Shardsinfo) LRCBeginDecode(OriginalCount uint16, ShardSize uint32) unsafe.Pointer {
	PRecoveryData := C.malloc(C.size_t(C.uint(ShardSize) * C.uint(OriginalCount)))
	s.ShardSize = ShardSize
	s.OriginalCount = OriginalCount
	s.PRecoveryData = PRecoveryData
	handle := C.LRC_BeginDecode(C.ushort(s.OriginalCount), C.ulong(s.ShardSize), (unsafe.Pointer)(PRecoveryData))
	s.Handle = handle
	return handle
}

func (s *Shardsinfo) LRCDecode(Dshard Shard) (OShardData []byte, res int) {
	res = int(C.LRC_Decode(s.Handle, unsafe.Pointer(&Dshard[0])))
	if res > 0{
		Osize := (s.ShardSize-1) * uint32(s.OriginalCount)
		OShardData = make([]byte, Osize)
		C.getCmemData(unsafe.Pointer(&OShardData[0]), s.PRecoveryData, C.int(Osize))
		C.free(s.PRecoveryData)
		C.LRC_FreeHandle(s.Handle)
	}
	if res < 0{
		C.free(s.PRecoveryData)
		C.LRC_FreeHandle(s.Handle)
	}
	return
}

//use to free recoverData after encode
func (s *Shardsinfo) FreeRecoverData(){
    C.free(s.PRecoveryData)
}
