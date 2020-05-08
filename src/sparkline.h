/* sparkline.h -- ASCII Sparklines header file
 * 1、Sparklines：直接翻译为"波形图" "迷你图" "走势图",专业一点的翻译叫“微线图”
 * 2、在集成到redis之前是一个独立的项目，地址为“https://github.com/antirez/aspark”
 * 2、功能 ：定一批值 + 值对应的标签，以字符作为像素点 在窗口上将这种值和标签的对应关系表现出来，用字符生成折线图。
 *  github示例如下：
 *  $ ./aspark 1,2,3,4,10,7,6,5
 *    `-_
 * __-`   `
 *
 * By default the program prints graphs using two rows. For better resolution you
 * can change this using the --rows option:
 *
 * $ ./aspark 1,2,3,4,5,6,7,8,9,10,10,8,5,3,1 --rows 4
 *        _-``_
 *      _`
 *    -`       `
 * _-`          `_
 *
 * Sometimes graphs are more readable if the area under the curve is filled,
 * so a --fill option is provided:
 *
 * $ ./aspark 1,2,3,4,5,6,7,8,9,10,10,8,5,3,1 --rows 4 --fill
 *        _o##_
 *      _#|||||
 *    o#|||||||#
 * _o#||||||||||#_
 *
 * It is possible to use labels, specifying them using a ':' character followed
 * by the label in the list of comma separated values, like in the following
 * example:
 *
 * $ ./aspark '1,2,3,4,5:peak,4,3,1,0:base'
 *   _-`-_
 * -`     -_
 *
 *     p   b
 *     e   a
 *     a   s
 *     k   e
 *
 * Sometimes a logarithmic scale is to be preferred since difference betwen values
 * can be too big:
 *
 * $ ./aspark 1,2,3,10,50,100
 *      `
 * ____`
 *
 * $ ./aspark 1,2,3,10,50,100 --log
 *     -`
 * __-`
 *
 * Stream mode
 * ---
 *
 * In stream mode data is read form standard input, one value per each line:
 *
 * $ echo -e "1\n2\n3\n" | ./aspark --stream
 *  _`
 * _
 *
 * In this mode it is still possible to use labels, using a space and the label
 * after the actual value, like in the following example:
 *
 * $ echo -e "1\n2 foo\n3\n" | ./aspark --stream
 *  _`
 * _
 *
 *  f
 *  o
 *  o
 *
 * In stream mode it is often interesting to pipe data form other programs:
 *
 * $ ruby -e '(1..40).each{|x| print Math.sin(x/2),"\n"}' | ./aspark --stream --rows 4 --fill
 *  ####        __##          ##__        #
 *  ||||__      ||||##      ##||||      __|
 * #||||||    oo||||||      ||||||oo    |||
 * |||||||oo__||||||||##__##||||||||__oo|||
 *
 * Characters frequency mode
 * ---
 *
 * The last mode is enabled usign --txtfreq or --binfreq options. It is used to
 * create a frequency table of the data received from standard input:
 *
 * $ cat /etc/passwd | ./aspark --txtfreq --fill --rows 4
 *               o          #          #            _
 *               |          |      #   |            |#_
 *               |          |      |_  |   #  __#o_ |||__
 * _________#____|__#_______|______||##|#o_|__|||||_|||||__#_
 *
 * !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ
 *
 * You can see the frequency of every single byte using --binfreq.
 *
 * Check aspark --help for more information about the usage.
 *
 * ---------------------------------------------------------------------------
 *
 * Copyright(C) 2011-2014 Salvatore Sanfilippo <antirez@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SPARKLINE_H
#define __SPARKLINE_H

/* A sequence is represented of many "samples" */
struct sample {
    double value;
    char *label;
};

struct sequence {
    int length;
    int labels;
    struct sample *samples;
    double min, max;
};

#define SPARKLINE_NO_FLAGS 0
#define SPARKLINE_FILL 1      /* Fill the area under the curve. */
#define SPARKLINE_LOG_SCALE 2 /* Use logarithmic scale. */

struct sequence *createSparklineSequence(void);
void sparklineSequenceAddSample(struct sequence *seq, double value, char *label);
void freeSparklineSequence(struct sequence *seq);
sds sparklineRenderRange(sds output, struct sequence *seq, int rows, int offset, int len, int flags);
sds sparklineRender(sds output, struct sequence *seq, int columns, int rows, int flags);

#endif /* __SPARKLINE_H */
