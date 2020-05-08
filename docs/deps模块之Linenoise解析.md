# deps模块之linenoise

## 什么是linenoise

> 今天解锁一个开源的REPL工具——Linenoise。Linenoise是可以完全代替readline的，非常轻量级的命令行处理工具。Redis，MongoDB和Android都将Linenoise作为命令行解析工具，那么今天我们就来解锁这个开源的命令行处理工具，也许某一天在你的项目里会派上用场。

## 特性

- 支持单行和多行编辑模式，实现了常用的键绑定。
- 支持历史命令记录
- 支持命令不全
- 支持命令提示
- 超轻量级，大约1100行代码（readline大约30,000行代码）
- 非常方便的融入你的项目李（一个.h，一个.c）

## 常用API

- > Linenoise非常易于使用，阅读该库附带的示例将使您尽快熟悉。以下是API调用及其使用方式。

  ```
  char *linenoise(const char *prompt);
  ```

  该接口现实命令提示符如hello> ，并返回用户输入缓冲区，当内存不足活文件结尾是返回NULL。
  使用该接口循环接受用户输入命令并现实提示符

  ```
  while((line = linenoise("hello> ")) != NULL) {
      printf("You wrote: %s\n", line);
      linenoiseFree(line); /* Or just free(line) if you use libc malloc. */
  }
  ```

- ## 单行 or 多行编辑

默认情况下linenoise使用单行编辑，如果需要输入大量内容可以开启多行编辑模式。

```c
linenoiseSetMultiLine(1);
```

## 命令历史

Linenoise支持历史记录，因此用户不必一次又一次地键入相同的内容，而是可以使用向下和向上箭头来搜索和重新编辑已经插入的命令。——个人觉得这个功能非常有用。

```
// 将一条你执行的命令添加到历史，从而你可以通过上下键找到它
int linenoiseHistoryAdd(const char *line);
// 要使用历史记录，您必须设置历史记录的长度（默认情况下为零，因此，如果未设置正确的长度，则将禁用历史记录）
int linenoiseHistorySetMaxLen(int len);
// Linenoise直接支持将历史记录保存到历史记录文件中，通过下面两个接口可以存取历史文件内容
int linenoiseHistorySave(const char *filename);
int linenoiseHistoryLoad(const char *filename);
```

下面是一段范例代码

```
    while((line = linenoise("hello> ")) != NULL) {
        /* Do something with the string. */
        if (line[0] != '\0' && line[0] != '/') {
            printf("echo: '%s'\n", line);
            linenoiseHistoryAdd(line); /* Add to the history. */
            linenoiseHistorySave("history.txt"); /* Save the history on disk. */
        } else if (!strncmp(line,"/historylen",11)) {
            /* The "/historylen" command will change the history len. */
            int len = atoi(line+11);
            linenoiseHistorySetMaxLen(len);
        } else if (line[0] == '/') {
            printf("Unreconized command: %s\n", line);
        }
        free(line);
    }
```

## 自动补全

Linenoise支持自动补全功能，即用户按下``键时会自动补全你的命令。
为了实现这一功能，你可以注册并实现你的补全函数。

```
// 注册自动补全函数completion
linenoiseSetCompletionCallback(completion);
```

注册的补全必须是一个`void`返回值函数，需要传入一个`const char`指针（该指针是用户到目前为止已键入的命令字符）和一个`linenoiseCompletions`对象指针，该对象指针用作是`linenoiseAddCompletion`将回调添加到补全中的参数。一个例子将使其更加清楚：

```
void completion(const char *buf, linenoiseCompletions *lc) {
    if (buf[0] == 'h') {
        linenoiseAddCompletion(lc,"hello");
        linenoiseAddCompletion(lc,"hello there");
    }
}
```

用户输入'h'是会现实补全列表"hello","hello there"。

## 命令提示

Linenoise具有称为`提示`的功能，当你使用Linenoise实现REPL时，这个功能非常有用。
当用户键入时，该功能会在光标的右侧显示可能有用的提示。提示可以使用与用户键入的颜色不同的颜色显示，也可以加粗。
例如，当用户开始输入`"git remote add"`提示时，可以在提示的右侧显示字符串` `。
改接口与自动补全功能类似：

```
linenoiseSetHintsCallback(hints);
```

回调函数实现如下：

```
char *hints(const char *buf, int *color, int *bold) {
    if (!strcasecmp(buf,"git remote add")) {
        *color = 35;
        *bold = 0;
        return " <name> <url>";
    }
    return NULL;
}
```

颜色编码：

```
red = 31
green = 32
yellow = 33
blue = 34
magenta = 35
cyan = 36
white = 37;
```

## 清屏

有时候你可能想通过输入某条命令来清屏，比如hello> `clear`，那么可以使用下面的接口：

```
void linenoiseClearScreen(void);
```