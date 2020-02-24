#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include "../YTLRC.h"
int RebuildTest(int originalCount, int iLost, int recoveryCount, int numLoops, int shardSize)
{
    printf("-----Start test LRC_Initial with globalRecoveryCount=34,maxHandles=3 --------\n");
    if ( !LRC_Initial(recoveryCount, 10) ) {
       printf("   LRC_Initial failed\n");
       return 0;
   }
   printf("  LRC_Initial ok\n"); 
   printf("----- Start test LRC_BeginRebuild with originalCount=110,iLost=6,shardSize=16384,*pData-------------\n");
   uint8_t * rebuilddata = (malloc(shardSize));
   short handle=LRC_BeginRebuild(originalCount, iLost, shardSize, rebuilddata);
   if (handle < 0){
      printf("   LRC_BeginRebuild failed and return handle is %d\n",handle);
      return 1;
   }
   printf("   LRC_BeginRebuild ok and return handle is %d\n",handle);
   printf("----- Start test LRC_NextRequestList with  handle  to get shards list of needed shards index----------\n"); 
   uint8_t needlist[256];
   int n = LRC_NextRequestList(handle,needlist);
   if (n < 0){
       printf("   something wrong\n");
       return 1;
   }
   int i;
   printf("   Return the number of needed shards is %d:\n", n);
   for (i=0; i < n; i++){
       printf("shard index:%d\n", (int)needlist[i]);
   }
   printf("----- Start test LRC_OneShardForRebuild with  handle and new needed shards to get shards list of needed shards index----------\n");
   FILE *fp;
   short ret;
   char dir[128];
   char * shardbuf=(char*)(malloc(shardSize));
   for (i=0;i < n;i++){
      sprintf(dir,"../testdata/test%d.dat",(short)needlist[i]);
      if((fp=fopen(dir,"rb"))==NULL)
      {
         printf("   file %s  cannot open \n",dir);
         fclose(fp);
         return 1;
      }
      ret=0;
      fread(shardbuf, shardSize, 1, fp);
      fclose(fp);
      ret=LRC_OneShardForRebuild(handle,shardbuf);
      if (ret >0)
         break;
      if (ret < 0){
         printf("   file %s  add to rebuilder error \n",dir);
         return 1;
      }
   }
   if (ret == 0){
      printf("needed more shards!!!\n");
      return 1;
   }
   printf("shard data rebhuild complete\n");
   sprintf(dir,"./rebuild_test%d.dat", iLost);
   if (!access(dir,0))
      remove(dir);
   fp=fopen(dir, "wb");
   fwrite(rebuilddata,16384,1,fp);
   fclose(fp);
}

int main(int argc, const char *argv[])
{
   
   int argv1;
   short ret1,handle;
   if (argc < 1) {
      printf("Please input parameter!!!\n");
      return 0;
   }
   sscanf(argv[1], "%d", &argv1);
   switch(argv1){
     case 1:
        break;
     case 2:
        break;
     case 3:
       if (argc != 7){
          printf("Please input parameters:int testtype,int originalCount, int iLost, int recoveryCount, int numLoops, int shardSize!!!\n");
          break;
       }
       int originalCount,iLost,recoveryCount,numLoops,shardSize;
       sscanf(argv[2], "%d", &originalCount);
       sscanf(argv[3], "%d", &iLost);
       sscanf(argv[4], "%d", &recoveryCount);
       sscanf(argv[5], "%d", &numLoops);
       sscanf(argv[6], "%d", &shardSize);
       RebuildTest(originalCount,iLost,recoveryCount,numLoops,shardSize);
       break;
     default:
       break;
   }
   return 0;   
}
