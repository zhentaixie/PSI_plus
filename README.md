# PSI_plus
Experimental Test Code for Circuit PSI

本仓库为论文【["Unbalanced Circuit-PSI from Oblivious Key-Value Retrieval"](https://eprint.iacr.org/2023/1636)】的测试性c++代码，目前未完成，仅做备份参考用。论文原作者对应可用源代码请参考：https://github.com/alibaba-edu/mpc4j.git 。

目前包含两份开源代码，测试本仓库前事先完成两份开源代码的编译。按照Kunlun库中的说明进行编译，后续编译过程可使用setup.sh进行辅助。本仓库对2Pc-Cuircuit-PSI中隐私相等性测试做出扩展，修改后的执行文件为./psi_ca/circuit_psi.cpp。

https://github.com/shahakash28/2PC-Circuit-PSI.git

https://github.com/yuchen1024/Kunlun.git

目前已添加OT，仍在测试，后续需要添加BatchPIR，

不经意传输：需要使用libOTe，对应开源库https://github.com/osu-crypto/libOTe.git

BatchPIR：对应开源库https://github.com/mhmughees/vectorized_batchpir

注意：两份开源代码的编译需要至少g++版本达到11，cmake版本达到3.15.0。



目前正在使用以及预计使用第三方库统计：

- https://github.com/shahakash28/2PC-Circuit-PSI.git

  - boost

  - libgmp-dev

  - libssl-dev

  - libntl-dev

  - libglib2.0-dev
  - https://github.com/encryptogroup/ABY.git
  - https://github.com/shahakash28/EzPC-Aligned.git
  - https://github.com/Oleksandr-Tkachenko/HashingTables.git
  - libOTe

- https://github.com/yuchen1024/Kunlun.git
  - openssl（版本3.0 并修改一行源代码）
  - openmp (libomp)

- https://github.com/osu-crypto/libOTe.git
  - [coproto](https://github.com/Visa-Research/coproto)
    - function2：https://github.com/Naios/function2.git
    - macoro：https://github.com/ladnir/macoro.git
    - span-lite： https://github.com/martinmoene/span-lite.git
  - [libsodium](https://doc.libsodium.org/), [Relic](https://github.com/relic-toolkit/relic), or [SimplestOT](https://github.com/osu-crypto/libOTe/tree/master/SimplestOT)
  - boost
  - openssl
- https://github.com/mhmughees/vectorized_batchpir
  - seal：https://github.com/microsoft/SEAL.git

