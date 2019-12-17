package main
import "lrcpkg"
import "fmt"
import "os"
import "io/ioutil"
import "strconv"
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
   lrctype.Handle=-1    //handle,do not cares the init value
   lrctype.Status=0     // repaired data status
   lrctype.RecoverNum = 34   // number of recover shrads 
   lrctype.RebuilderNum = 3  
   lrctype.OriginalCount =110 // number of data shards
   lrctype.Lostindex = 6      // the index of lost shard
   lrctype.ShardSize = 222    //do not cares the init value
   lrc=lrctype
   a:=lrc.GetRCHandle(&lrctype)
   slist,s := lrc.GetNeededShardList(a)
   if (s < 0){
     fmt.Println("something wrong")
     lrc.FreeHandle(&lrctype)
     return
   }
   if (s == 0){
     fmt.Println("can not repair the data")
     lrc.FreeHandle(&lrctype)
     return
   }
   fmt.Println(s)
   var z int16
   for e:=slist.Front();nil != e;e=e.Next() {
       z = 0
       f,_ := os.Open("/root/LRC/test" + strconv.Itoa(int(e.Value.(int16))) + ".dat")
       t,_:=ioutil.ReadAll(f)
       f.Close()
       z=lrc.AddShardData(a,t)
       if (z == 0) {
          fmt.Println("add data test" + strconv.Itoa(int(e.Value.(int16))) + ".dat ok")
       }
       if (z < 0){
          fmt.Println("add data test" + strconv.Itoa(int(e.Value.(int16))) + ".dat error")
       } 
       if (z > 0){
          fmt.Println("add data test" + strconv.Itoa(int(e.Value.(int16))) + ".dat ok")
          fmt.Println("shard has been repaired!!!")
          break
       }
   }
   if z < 0 {
      fmt.Println("Can not rebuild data!!!")
      return
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
