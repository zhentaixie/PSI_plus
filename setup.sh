#!/bin/bash

# 进入2PC-Circuit-PSI目录
cd extern/2PC-Circuit-PSI

# 创建build目录
mkdir build

# 进入build目录
cd build

# 运行cmake
cmake ..

# 复制aux_hash目录下的所有文件到extern/HashingTables/cuckoo_hashing目录
cp ../aux_hash/* ../extern/HashingTables/cuckoo_hashing/.

# 进入debug_files目录并运行replace_files.sh脚本
cd ../../../debug_files
./replace_files.sh

# 进入psi_ca目录并运行copy_files.sh脚本
cd ../psi_ca
./copy_files.sh

# 返回到2PC-Circuit-PSI/build目录
cd ../extern/2PC-Circuit-PSI/build

# 编译项目
make -j

mkdir -p ../../../bin
cp ./bin/gcf_psi ../../../bin/


