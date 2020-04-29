# redis项目结构

## 总体结构

> -|deps
> 	-|hiredis			:C语言客户端
> 	-|lua				:lua脚本相关
> 	-|linenoise			：处理指令的交互窗口	
> 	-|jemalloc			：内存分配
> -|src					：redis具体基本核心功能实现(数据结构，命令操作，启动停止等)
> -|utils
> 	 -|create-cluster	：集群实现
>    	 -|graphs			
>     	-|hashtable			：hashtable实现
>     	-|hyperloglog		
>     	-|lru				：lru算法相关
>     	-|releasetools		
>    	 -|srandmember
> -|tests

