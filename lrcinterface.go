package lrcpkg
import "C"
import "unsafe"
import "container/list"
type Shardsinfo struct {
        Handle unsafe.Pointer  //操作句柄
        Status int16  //记录状态，1 初始化后正在进行重建，100 重建完成， -1 重建失败
	RecoverNum int16  //校验快总数
	RebuilderNum int16  //解码器最大允许数
	OriginalCount uint16 //原始数据最大碎片数
        Lostindex uint16  //希望恢复块的序号
	ShardSize uint32 //碎片大小,当前默认是16k 
        PtrData *C.char //重建后数据的保存buf
}

type LRCEngine interface {
        LRCinit(n int16)(int16)
	GetRCHandle(sdinf *Shardsinfo) (unsafe.Pointer) //创建碎片任务
	GetNeededShardList(handle unsafe.Pointer)(* list.List,int16) //获得恢复指定碎片所需碎片的列表，建议加载完所有已知碎片后再调用
        AddShardData(handle unsafe.Pointer,sdata []byte)(int16) //每获得一个碎片就可以加入，大于0表示已经重建完毕，等于0表示还需要碎片，小于0表示有错误
	GetRebuildData(sdinf *Shardsinfo)([]byte,int16) //获取恢复后的碎片及状态，大于0表示数据获取成功，等于0表示还需要碎片，小于0表示错误
        FreeHandle(sdinf *Shardsinfo) //恢复中期，放弃恢复任务或者任务结束时，需要调用本函数
}
