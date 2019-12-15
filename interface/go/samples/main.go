package main
import "lrcpkg"
import "fmt"
import "os"
import "io/ioutil"
func checkFileIsExist(filename string) bool {
    var exist = true
    if _, err := os.Stat(filename); os.IsNotExist(err) {
        exist = false
    }
    return exist
}
func check(e error) {
    if e != nil {
        panic(e)
    }
}
func main(){
   var lrc lrcpkg.LRCEngine
   var lrctype lrcpkg.Shardsinfo
   lrctype.Handle=-1
   lrctype.Status=0
   lrctype.RecoverNum = 34
   lrctype.RebuilderNum = 3
   lrctype.OriginalCount =110
   lrctype.Lostindex = 6
   lrctype.ShardSize = 222
   lrc=lrctype
   a:=lrc.GetRCHandle(&lrctype)
   lrc.GetNeededShardList(a)
//   lrc.FreeHandle(&lrctype)
   f0,_ := os.Open("/root/LRC/test0.dat")
   t0,_:=ioutil.ReadAll(f0)
   f0.Close()
   a0:=lrc.AddShardData(a,t0)
   if (a0 > 0 || a0 == 0) {
      fmt.Println("add data test0.dat ok")
   } 
   f1,_ := os.Open("/root/LRC/test1.dat")
   t1,_:=ioutil.ReadAll(f1)
   f1.Close()
   a1:=lrc.AddShardData(a,t1)
   if (a1 > 0 || a1 == 0) {
      fmt.Println("add data test1.dat ok")
   } 
   f2,_ := os.Open("/root/LRC/test2.dat")
   t2,_:=ioutil.ReadAll(f2)
   f2.Close()
   a2:=lrc.AddShardData(a,t2)
   
   if (a2 > 0 || a2 == 0) {
      fmt.Println("add data test2.dat ok")
   } 
   f3,_:= os.Open("/root/LRC/test3.dat")
   t3,_:=ioutil.ReadAll(f3)
   f3.Close()
   a3:=lrc.AddShardData(a,t3)
   if (a3 > 0 || a3 == 0) {
      fmt.Println("add data test3.dat ok")
   } 
   f4,_:= os.Open("/root/LRC/test4.dat")
   t4,_:=ioutil.ReadAll(f4)
   f4.Close()
   a4:=lrc.AddShardData(a,t4)
   if (a4 > 0 || a4 == 0) {
      fmt.Println("add data test4.dat ok")
   } 

   f5,_:= os.Open("/root/LRC/test5.dat")
   t5,_:=ioutil.ReadAll(f5)
   f5.Close()
   a5:=lrc.AddShardData(a,t5)
   
   if (a5 > 0 || a5 == 0) {
      fmt.Println("add data test5.dat ok")
   } 
   f7,_ := os.Open("/root/LRC/test7.dat")
   t7,_:=ioutil.ReadAll(f7)
   f7.Close()
   a7:=lrc.AddShardData(a,t7)
   if (a7 > 0 || a7 == 0) {
      fmt.Println("add data test7.dat ok")
   } 

   f110,_ := os.Open("/root/LRC/test110.dat")
   t110,_:=ioutil.ReadAll(f110)
   f110.Close()
   a110:=lrc.AddShardData(a,t110)
   if (a110 > 0 || a110 == 0) {
      fmt.Println("add data test110.dat ok")
   } 
   g,k := lrc.GetRebuildData(&lrctype)
   if k > 0 {
     fmt.Println("Rebuild data ok")
   }else {
     fmt.Println("Rebuild data failed")

   }
   lrc.FreeHandle(&lrctype)
   fmt.Println(g[0])
    if checkFileIsExist("./rebuild_test6"){
      os.Remove("./rebuild_test6")
   }
   _,err1:= os.Create("./rebuild_test6")
   check(err1)
   err2 := ioutil.WriteFile("./rebuild_test6", g, 0666) //写入文件(字节数组)
   check(err2)  
  
}
