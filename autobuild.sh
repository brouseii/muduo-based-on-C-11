#!/bin/bash

# 告诉shell在执行任何命令时，如果出现了错误（非零返回值），则立即退出脚本
set -e

# 如果没有build目录，创建该目录
if [ ! -d `pwd`/build ]; then
    mkdir `pwd`/build
fi

rm -rf `pwd`/build/*

cd `pwd`/build &&
    cmake .. &&
    make

# 回到项目根目录
cd ..

# 把头文件拷贝到/usr/include/mymuduo、so库拷贝到/usr/lib
if [ ! -d /usr/include/mymuduo ]; then 
    mkdir /usr/include/mymuduo
fi

cd `pwd`/include
for header in `ls *.h`
do
    cp $header /usr/include/mymuduo
done

cd ..
cp `pwd`/lib/libmymuduo.so /usr/lib


# 更新默认共享库缓存
ldconfig