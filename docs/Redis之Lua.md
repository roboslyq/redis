# Redis之Lua

# Lua基本概念

>1. Lua 是一个小巧的[脚本语言](https://www.baidu.com/s?wd=脚本语言&tn=SE_PcZhidaonwhc_ngpagmjz&rsv_dl=gh_pc_zhidao)。来 其设计目的是为了嵌入应用程序中，从而为应用程序提供灵活的扩展和定制功能。Lua由标准C编写而成，几乎在所有[操作系统](https://www.baidu.com/s?wd=操作系统&tn=SE_PcZhidaonwhc_ngpagmjz&rsv_dl=gh_pc_zhidao)和平台上都可以编译，运行。Lua并没有提供强大的库，这是由它的定位决定的。Lua 有一个同时进行的JIT项目，提供在特定平台上的即时编译功能。
>2. Lua脚本可以很容易的被[C/C++](https://www.baidu.com/s?wd=C%2FC%2B%2B&tn=SE_PcZhidaonwhc_ngpagmjz&rsv_dl=gh_pc_zhidao) 代码调用源，也可以反过来调用[C/C++](https://www.baidu.com/s?wd=C%2FC%2B%2B&tn=SE_PcZhidaonwhc_ngpagmjz&rsv_dl=gh_pc_zhidao)的函数，这使得Lua在应用程序中可以被广泛应用。不仅仅作为扩展脚本，也可以作为普通的配置文件，zhidao代替XML,ini等文件格式，并且更容易理解和维护。 Lua由标准C编写而成，代码简洁优美，几乎在所有[操作系统](https://www.baidu.com/s?wd=操作系统&tn=SE_PcZhidaonwhc_ngpagmjz&rsv_dl=gh_pc_zhidao)和平台上都可以编译，运行。一个完整的Lua解释器不过200k，在目前所有脚本引擎中，Lua的速度是最快的。

总结:

- lua是一门新的解释型语言
- lua的解释器是用标准的c语言写的
- 因为解释器是标准C写的,所以可以很方便的与C语言进行集成,很容易被C/C++调用,也很容易调用C库

## Lua环境搭建(windows)

> 前提：安装好cygwin,具体安装就不详细解释了，请百度。

​	官网地址： https://www.lua.org/download.html ，很小，才200多K。

![lua1](./images/lua/1.jpg)

> 当然可以点击"get a binary"获取已经编译好的版本
>
> ![lua1](./images/lua/1_1.jpg)

​	下载完成后解压

![lua1](./images/lua/2.jpg)

> **lua静态库**：在VS下新建工程，选择生成静态库、不需要预编译头，包含src的文件，除了lua.c、luac.c
> **lua编译器**：在VS下新建工程，选择生成控制台工程，包含src的文件，除了lua.c
> **lua解释器**：在VS下新建工程，选择生成控制台工程，包含src的文件，除了luac.c

打开cygwin,进入解压目录 ，然后输入make编译

![lua1](./images/lua/3.jpg)

> lua编译需要指定平台，此处我是在windows下模块，选择mingw平台。

编译完成后，会生成exe文件，双击运行

![lua1](./images/lua/4.jpg)

> 缺少cygwin1.dll，将cygwin安装路径下bin目录的文件cygwin1.dll文件copy到当前目录即可。

![lua1](./images/lua/5.jpg)

## 环境变量配置

如果想在cmd环境正常使用lua命令，则需要配置相关环境变量

![lua1](./images/lua/9.jpg)

配置好后，测试结果如下：

![lua1](./images/lua/10.jpg)

## 交互式编程

直接启动lua.exe客户端，在界面输入相关命令，即可正常输出。

![lua1](./images/lua/6.jpg)

> ./lua.exe是正常make之后生成的可执行文件

## 脚本式编程

新建脚本文件，以.lua结尾

![lua1](./images/lua/7.jpg)

编辑.lua文件

```lua
print("Hello World！")
print("www.roboslyq.com")
```

运行

![lua1](./images/lua/11.jpg)



# Lua怎么与C交互

最近lua很火，因为《愤怒的小鸟》使用了lua，ios上有lua解释器？它是怎么嵌入大ios中的呢？lua的官网说："lua is an embeddable scripting language"，怎么理解呢？怎么在你自己的程序里嵌入lua解释器呢？如果可以在我的程序中嵌入了lua，那是否意味着我可以从此用lua编程了呢？

带着这些问题，打算在我的windows笔记本上做个实验，目标是在windows上跑一个lua的解释器，然后用lua语言写一个程序，跑在这个解释器之上，也就说最后的效果是:

（上） lua program -> lua解释器 -> windows （下）

或者是：

（上） lua program -> lua解释器 -> my c program -> windows （下）

实践：

1） 下载lua源码；

2） 用eclipse+mingw编译lua解释器的源码

eclipse+mingw编译环境我之前就已经搭好了，有兴趣的同学可以参考我的另一篇文章，当然你也可以用其他编译工具，比如visual Studio；

建立一个空白c工程，project type选Executable->Empyt Project；

导入src目录下的所有文件，包括.c, .h和Makefile文件；

点击build，开始编译；

遇到编译错误：main()重定义，lua.c和luac.c里分别有一个main()，lua.c是解释器的源码，luac.c是编译器的源码，我们此时要的是解释器，所以从工程删除luac.c，重新编译，通过，生成lua.exe；

没想到lua的源码和Makefile写得这么好，这么顺利就编译通过了，网上说lua是用标准c写的，只要是支持标准c的编译环境都能顺利的编译它；

3） 运行编译出来的解释器

找到lua.exe所在的目录，在这个目录下先准备一个用lua写的程序，比如hello.lua，包含代码：print "hello lua"；

打开一个windows的cmd窗口，cd到编译结果lua.exe所在的目录，调用lua解释器，执行hello.lua程序，命令为：lua.exe "hello.lua"，（我之前从官网下载了一个编译好的lua解释器和SciTE编辑器，我看到SciTE编辑器就是用这个命令调用lua解释器的）看到窗口输出hello lua，太好了，至此，我亲手编出了一个lua解释器，可以跑在我的windows系统之上；

4） 下一步，怎么在我的c程序里调用lua解释器？

哦，知道了，我的c程序调用windows的接口将lua.exe跑起来，同时把hello.lua文件作为参数传给lua.exe，不就行了。

在你的main.c中，可以用类似这样的代码调用lua.exe：system("C:\\some_path\\lua.exe -e \"hello.lua\" " )

5） lua解释器执行hello.lua的时候，如果hello.lua想调用一个本地库foo.dll中的foo()函数，foo.dll是foo.c编译生成的，怎么办？

稍等，我正在做...

6) foo.c怎么调用hello.lua中的函数？

写到这儿，突然明白了，解释型语言和编译型语言之间的相互调用没什么神秘的，还是以lua和c为例，

a）先看lua怎么调c？

所谓的lua调用c，是指foo.lua调用bar.c中的代码，foo.lua本身只是一段源码，不能执行的，必须由lua解释器来执行，lua解释器是一个可执行程序，可以运行，foo.lua可以看成是解释器程序运行过程中读取的一个配置文件，根据”配置文件foo.lua“内容的不同做不同的事情，如果”配置文件foo.lua“说调用bar.c中的bar()函数，那么解释器lua.exe就去调用了，假设此时bar.c也已经编译成了二进制bar.dll，那既然lua.exe和bar.dll是同一个平台（比如windows）上的二进制程序，互相之间的调用就没什么稀奇了吧，就跟你写一个简单的main.exe调用bar.dll一样了；

b）再看c怎么调用lua？

所谓的c调用lua，是指foo.c调用bar.lua中的代码，我们先要把foo.c编译成一个可执行程序foo.exe，这样才能执行嘛，可是被调用方bar.lua只是一个包含lua代码的源文件，不是二进制的（我们现在是在讨论解释器，不考虑lua预编译成二进制的情况），无法调用，所以我们就要借用lua解释器了，所以整个流程就是：foo.exe -call-> lua解释器（可执行程序）-读取-> bar.lua，foo.exe其实只和lua解释器打交道，告诉它我要调用某个函数，然后由解释器负责去找到这个函数并调用；

综上，你会发现，不管是lua调c还是c调lua，他们都不是直接交互的，因为lua程序只是一堆”死“的源代码，像一个配置文件一样，不能执行的，所以就需要一个中间人”lua解释器“，只要c程序和lua解释器之间约定好接口，那么实现c和lua之间的调用就是很自然的事情了，没什么神秘的。

最后给出一个示意图：

一个用c写的程序，已经编译成二进制 <--> 用c写的lua解释器，已经编译成二进制 -> 一个lua写的程序，只能被解释器”被动“地读取，不能”主动“地执行

## Clion中开发lua

> 安装lua插件即可??

# Redis中的Lua

## 集成原理 

## 使用方式

## 常见应用场景 

# 参考资料

 https://www.cnblogs.com/wangchengfeng/p/3716821.html 

 https://blog.csdn.net/follow_blast/article/details/81735632 

 https://segmentfault.com/a/1190000022162774 