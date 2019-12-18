package lrcpkg
/*
 #cgo CFLAGS: -I./
 #cgo CFLAGS: -g -Wall -O3
 #cgo CFLAGS: -Wunused-variable
// #cgo CFLAGS: -Wrestrict
 #cgo CFLAGS: -std=c99
// #cgo LDFLAGS:-L ./ -lYTLRC -lcm256 -lgf256
 #cgo amd64 CFLAGS: -mssse3
 #cgo arm arm64 CFLAGS: -DLINUX_ARM=1
 #cgo LDFLAGS: -lm
 #include <stdlib.h>
 #include "./YTLRC.h"
 #include "./cm256.h"
 #include "./gf256.h"
*/
import "C"
import "unsafe" 
import (
//     "fmt"
     "container/list"
)
//var datalist[] C.char *
func (s Shardsinfo)GetRCHandle(sdinf *Shardsinfo) (int16){
     if (sdinf == nil) {
        return -1
     }
     
     stat := C.LRC_Initial(C.short(sdinf.RecoverNum),C.short(sdinf.RebuilderNum))
     if (stat < 0){
         return -2
     }
     
/*
     sdinf.ptrData = make([]byte,16384)
     for i:=0;i < sdinf.rebuilderNum;i++{
         datalist[i]=(*C.char)(C.malloc(C.size_t(16384)))
     }
*/
     sdinf.PtrData = (*C.char)(C.malloc(C.size_t(16384)))
     sdinf.ShardSize=16384
     if sdinf.PtrData == nil {
        panic("ptrData malloc failed!\n")
     }
     
     handle := C.LRC_BeginRebuild(C.ushort(sdinf.OriginalCount),C.ushort(sdinf.Lostindex),16384,unsafe.Pointer(sdinf.PtrData))
     if (handle < 0){
        return -3
     }
     sdinf.Status=1
     sdinf.Handle=int16(handle)
//     fmt.Println(sdinf.Handle)    
     return int16(handle)
}

func (s Shardsinfo)GetNeededShardList(handle int16)(*list.List,int16){
     var needlist *C.char
     needlist = (*C.char)(C.malloc(C.size_t(256)))
     oll := list.New()
     var ndnum C.short
     ndnum = C.LRC_NextRequestList(C.short(handle),needlist)
     //var cchar C.char
     if (int16(ndnum) >0){
        for i:=C.short(0); i < ndnum; i++ {
            oll.PushBack(int16(*(*C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(needlist)) + uintptr(i)))))
            //cchar = needlist[i]
            //oll.PushBack(int16(cchar))
        }
     }
     C.free(unsafe.Pointer(needlist))
     return oll,int16(ndnum)
}

func (s Shardsinfo)AddShardData(handle int16,sdata []byte)(int16){
     var stat C.short
     var j uint32
     j=0
     temp := (*C.char)(C.malloc(C.size_t(16384)))
  //   fmt.Println("len=%d",len(sdata))
     for i:=C.uint(0);i < C.uint(len(sdata));i++{
        *(*C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(temp)) + uintptr(i)))=C.char(sdata[j])
        j++
     }
     //fmt.Println(*(*C.char)(unsafe.Pointer(temp)))
    // fmt.Println("ahahahahahah")
     stat = C.LRC_OneShardForRebuild(C.short(handle),unsafe.Pointer(temp))
     C.free(unsafe.Pointer(temp))
     return int16(stat)
}

func (s Shardsinfo)GetRebuildData(sdinf *Shardsinfo)([]byte,int16){
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

func (s Shardsinfo)FreeHandle(sdinf *Shardsinfo){
     C.LRC_FreeHandle(C.short(sdinf.Handle))
     if (sdinf.PtrData !=nil){
        C.free(unsafe.Pointer(sdinf.PtrData))
     }
}
