#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../YTLRC.h"
int main(int argc, const char *argv[])
{
   
   int argv1;
   short ret1,handle;
   if (argc > 1) {
      sscanf(argv[1], "%d", &argv1);
   }
   printf("-----Start test LRC_Initial with globalRecoveryCount=34,maxHandles=3 --------\n");
   ret1=LRC_Initial(34,3);
   if (ret1 == 0){
       printf("   LRC_Initial failed\n");
       return 1;
   }
  printf("  LRC_Initial ok\n"); 
   printf("----- Start test LRC_BeginRebuild with originalCount=110,iLost=6,shardSize=16384,*pData-------------\n");
   char * rebuilddata;
   rebuilddata=(char*)(malloc(16384));
   memset(rebuilddata,0,16384);
   handle=LRC_BeginRebuild(110,6,16384,rebuilddata);
   if (handle < 0){
      printf("   LRC_BeginRebuild failed and return handle is %d\n",handle);
      return 1;
   }
   printf("   LRC_BeginRebuild ok and return handle is %d\n",handle);
   printf("----- Start test LRC_NextRequestList with  handle  to get shards list of needed shards index----------\n"); 
   char* needlist=(char*)(malloc(256));
   ret1=LRC_NextRequestList(handle,needlist);
   if (ret1 < 0){
       printf("   something wrong\n");
       return 1;
   }
   int i;
   printf("   Return the number of needed shards is %d:\n",ret1);
   for (i=0;i < ret1;i++){
       printf("shard index:%d\n",needlist[i]);
   }
   printf("----- Start test LRC_OneShardForRebuild with  handle and new needed shards to get shards list of needed shards index----------\n");
   FILE *fp;
   short ret;
   char dir[40];
   char * shardbuf=(char*)(malloc(16384));
   for (i=0;i < ret1;i++){
      memset(dir,0,40);
      sprintf(dir,"../testdata/test%d.dat",(short)needlist[i]);
      if((fp=fopen(dir,"r"))==NULL)
      {
         printf("   file %s  cannot open \n",dir);
      }
      ret=0;
      memset(shardbuf,0,16384);
      read(fp,shardbuf,16384);
      fclose(fp);
      ret=LRC_OneShardForRebuild(handle,shardbuf);
      if (ret >0)
         break;
      if (ret < 0){
         printf("   file %s  add to rebuilder error \n",dir);
         return 1;
      }
   }
   if (ret >0){
      printf("shard data rebhuild complete");
   }
   memset(dir,0,40);
   sprintf(dir,"./rebuild_test%d.dat",argv1);
   if (!access(dir,0))
      remove(dir);
   fp=fopen(dir, "w");
   fwrite(rebuilddata,16384,1,fp);
   return 0;
}
