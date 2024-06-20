// Original Work copyright (c) Oleksandr Tkachenko
// Modified Work copyright (c) 2021 Microsoft Research
//
// \author Oleksandr Tkachenko
// \email tkachenko@encrypto.cs.tu-darmstadt.de
// \organization Cryptography and Privacy Engineering Group (ENCRYPTO)
// \TU Darmstadt, Computer Science department
//
// \copyright The MIT License. Copyright Oleksandr Tkachenko
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Modified by Akash Shah

#include <cassert>
#include <iostream>

#include <boost/program_options.hpp>

#include <ENCRYPTO_utils/crypto/crypto.h>
#include <ENCRYPTO_utils/parse_options.h>
#include <chrono>
#include "../../Kunlun/crypto/setup.hpp"
#include "../../Kunlun/mpc/oprf/vole_oprf.hpp"
#include "../../Kunlun/mpc/ot/iknp_ote.hpp"
#include "ENCRYPTO_utils/connection.h"
#include "ENCRYPTO_utils/socket.h"
#include "HashingTables/common/hash_table_entry.h"
#include "HashingTables/common/hashing.h"
#include "HashingTables/cuckoo_hashing/cuckoo_hashing.h"
#include "HashingTables/simple_hashing/simple_hashing.h"
#include "abycore/aby/abyparty.h"
#include "common/config.h"
#include "common/functionalities.h"

// https://stackoverflow.com/questions/24161243/how-can-i-add-together-two-sse-registers
inline block unsigned_lessthan(block a, block b) {
#ifdef __XOP__  // AMD XOP instruction set
  return _mm_comgt_epu64(b, a);
#else  // SSE2 instruction set
  block sign32 = _mm_set1_epi32(0x80000000);        // sign bit of each dword
  block aflip = _mm_xor_si128(b, sign32);           // a with sign bits flipped
  block bflip = _mm_xor_si128(a, sign32);           // b with sign bits flipped
  block equal = _mm_cmpeq_epi32(b, a);              // a == b, dwords
  block bigger = _mm_cmpgt_epi32(aflip, bflip);     // a > b, dwords
  block biggerl = _mm_shuffle_epi32(bigger, 0xA0);  // a > b, low dwords copied to high dwords
  block eqbig = _mm_and_si128(equal, biggerl);      // high part equal and low part bigger
  block hibig = _mm_or_si128(bigger, eqbig);   // high part bigger or high part equal and low part
  block big = _mm_shuffle_epi32(hibig, 0xF5);  // result copied to low part
  return big;
#endif
}
block add_with_carry(block x, block y) {
  // 执行加法操作
  block z = _mm_add_epi64(x, y);

  // 计算进位
  block c = _mm_unpacklo_epi64(_mm_setzero_si128(), unsigned_lessthan(z, x));

  // 处理进位
  z = _mm_sub_epi64(z, c);

  return z;
}
block invert_block(block a) {
  // 对__m128i寄存器中的每个元素进行按位取反
  // 创建一个全1的掩码
  block mask = _mm_cmpeq_epi32(_mm_setzero_si128(), _mm_setzero_si128());
  // 对a执行按位异或运算，即取反操作
  return _mm_xor_si128(a, mask);
}
block sub_with_borrow(block x, block y) {
  block opposite = add_with_carry(invert_block(y), Block::MakeBlock(0, 1));
  return add_with_carry(x, opposite);
}
struct VOLEOPRFTestCase {
  std::vector<block> vec_Y;  // client set
  std::vector<block> vec_Fk_Y;
  size_t INPUT_NUM;  // size of set
};

VOLEOPRFTestCase GenTestCase(size_t LOG_INPUT_NUM) {
  VOLEOPRFTestCase testcase;
  testcase.INPUT_NUM = 1 << LOG_INPUT_NUM;

  PRG::Seed seed = PRG::SetSeed(fixed_seed, 0);  // initialize PRG
  testcase.vec_Y = PRG::GenRandomBlocks(seed, testcase.INPUT_NUM);

  return testcase;
}
std::vector<uint8_t> oprf_server(NetIO &io) {
  CRYPTO_Initialize();

  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(7);  // 40 is the statistical parameter

  std::vector<uint8_t> oprf_key = VOLEOPRF::Server1(io, pp);

  return oprf_key;
}
std::vector<block> oprf_evaluate(std::vector<block> vec, std::vector<uint8_t> oprf_key) {
  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(7);  // 40 is the statistical parameter
  auto vec_Fk_X = VOLEOPRF::Evaluate1(pp, oprf_key, vec, vec.size());
  return vec_Fk_X;
}
std::vector<block> oprf_client(std::vector<block> vec, NetIO &io) {
  CRYPTO_Initialize();

  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(7);  // 40 is the statistical parameter

  std::vector<block> vec_Fk_Y = VOLEOPRF::Client1(io, pp, vec, pp.INPUT_NUM);
  return vec_Fk_Y;
}

void psi_ca_receiver(ENCRYPTO::PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock,
                     sci::NetIO *ioArr[3], osuCrypto::Channel &chl, NetIO &io, NetIO &io2) {
  auto start = std::chrono::steady_clock::now();
  VOLEOPRF::PP pp;
  pp = VOLEOPRF::Setup(22);
  // std::vector<block> tmp;
  // for (auto i = 0; i < 33; i++) {
  //   tmp.emplace_back(Block::MakeBlock(0, i));
  // }
  // std::vector<block> vec_Fk_Y = VOLEOPRF::Client1(io, pp, tmp, pp.INPUT_NUM);
  // std::vector<block> vec_Fk_X(pp.INPUT_NUM);
  // io.ReceiveBlocks(vec_Fk_X.data(), pp.INPUT_NUM);

  // if (Block::Compare(vec_Fk_Y, vec_Fk_X) == true) {
  //   PrintSplitLine('-');
  //   std::cout << "VOLEOPRF test succeeds" << std::endl;
  // } else {
  //   PrintSplitLine('-');
  //   std::cout << "VOLEOPRF test fails" << std::endl;
  // }

  // std::vector<__m128i> numbers;
  // for (auto i = 0; i < 16; i++) numbers.push_back(Block::zero_block);
  // numbers[0] = Block::all_one_block;
  // auto ans = perform_block_equality(numbers, context, sock, ioArr, chl);
  // for (auto i = 0; i < ans.size(); i++)
  //   if (ans[i] == 1)
  //     std::cout << "1 ";
  //   else
  //     std::cout << "0 ";
  PRG::Seed seed = PRG::SetSeed();  // initialize PRG
  std::vector<uint64_t> vec;
  auto item_num = 1ull << 20;
  for (auto i = 0; i < item_num; i++) vec.emplace_back(i);
  uint64_t bin_num = item_num * 1.27;
  uint64_t nbins = bin_num + (bin_num % 8 == 0 ? 0 : (8 - bin_num % 8));
  io.SendBytes(&nbins, 8);
  ENCRYPTO::CuckooTable cuckoo_table(static_cast<std::size_t>(nbins));
  cuckoo_table.SetNumOfHashFunctions(context.nfuns);
  cuckoo_table.Insert(vec);
  cuckoo_table.MapElements();
  // auto add = cuckoo_table.GetElementAddresses();
  if (cuckoo_table.GetStashSize() > 0u) {
    std::cerr << "[Error] Stash of size " << cuckoo_table.GetStashSize() << " occured\n";
  }
  auto idx_cuckoo_table = cuckoo_table.AsRawVectorNoID();
  auto idxs = std::get<0>(idx_cuckoo_table);
  auto cuckoo_table_v = std::get<1>(idx_cuckoo_table);
  // oprf
  std::vector<block> result = VOLEOPRF::Client1(io, pp, cuckoo_table_v, pp.INPUT_NUM);
  // Block::PrintBlock(cuckoo_table_v[0]);
  // for (auto i = 0; i < cuckoo_table_v.size(); i++) {
  //   std::cout << i << std::endl;
  //   Block::PrintBlock(cuckoo_table_v[i]);
  //   Block::PrintBlock(result[i]);
  // }
  // receive OKVS
  uint64_t baxos_size;
  io.ReceiveBytes(&baxos_size, 8);
  std::cout << baxos_size << std::endl;
  Baxos<gf_128> baxos(baxos_size, 1 << 18, 3);
  std::vector<block> okvs(baxos.total_size * baxos.bin_num, Block::zero_block);
  std::cout << "begin receive okvs:" << std::endl;

  io.ReceiveBlocks(okvs.data(), okvs.size());
  // Block::PrintBlocks(cuckoo_table_v);

  std::cout << "begin eq:" << std::endl;
  std::vector<block> decode_result(cuckoo_table_v.size());
  baxos.decode(cuckoo_table_v, decode_result, okvs, 8);
  // Block::PrintBlock(decode_result[0]);
  // Block::PrintBlock(decode_result[1]);
  // // decode OKVS

  for (auto i = 0; i < vec.size(); i++) {
    decode_result[i] ^= result[i];
  }

  std::vector<block> eq_blocks(nbins, Block::zero_block);
  for (auto i = 0; i < idxs.size(); i++) eq_blocks[idxs[i]] = decode_result[i];
  // Block::PrintBlocks(eq_blocks);

  auto ans = perform_block_equality(eq_blocks, context, sock, ioArr, chl);

  for (auto i = 0; i < ans.size(); i++)
    if (ans[i] == 1)
      std::cout << "1 ";
    else
      std::cout << "0 ";
  std::cout << std::endl;
  for (auto i = 0, j = 0; i < nbins; i++) {
    if (i == idxs[j]) {
      std::cout << "1 ";
      j++;
    } else {
      std::cout << "0 ";
    }
  }

  // auto pp_ot = ALSZOTE::Setup(BASE_LEN);
  // std::vector<uint8_t> tmp(384,1);
  // std::vector<block> vec_result_real = ALSZOTE::Receive(io, pp_ot, tmp, tmp.size());
  // std::cout << std::endl;
  auto pp_ot = IKNPOTE::Setup(BASE_LEN);
  for (auto i = 0; i < 128 - nbins % 128; i++) ans.emplace_back(0);
  std::cout << nbins + (128 - nbins % 128) << " " << ans.size() << std::endl;
  std::vector<block> vec_result_real = IKNPOTE::Receive(io2, pp_ot, ans, ans.size());
  block psi_ca_ans = Block::zero_block;
  for (auto i = 0; i < nbins; i++) {
    psi_ca_ans = add_with_carry(psi_ca_ans, vec_result_real[i]);
  }
  block psi_ca_tmp;
  io2.ReceiveBlock(psi_ca_tmp);
  auto block_ans = sub_with_borrow(psi_ca_ans, psi_ca_tmp);
  // Block::PrintBlock(block_ans);
  std::cout << ((uint64_t *)(&block_ans))[0] << std::endl;
  auto end = std::chrono::steady_clock::now();

  // 计算时间间隔
  std::chrono::duration<double> elapsed_seconds = end - start;
  std::cout << "Elapsed time: " << elapsed_seconds.count() << "s\n";
}
#include "../../Kunlun/crypto/aes.hpp"
void send_baxos(NetIO& io,std::vector<block>& key,std::vector<block>& value,uint64_t baxos_size){
  Baxos<gf_128> baxos(baxos_size, 1 << 18, 3);
  std::vector<block> encode_result(baxos.bin_num * baxos.total_size);
  std::cout << "begin solve" << std::endl;
  baxos.solve(key, value, encode_result, 0, 8);
  std::cout << "end solve" << std::endl;

  io.SendBlocks(encode_result.data(), encode_result.size());
}
// 1 0 0 1 0 1 0 1 0 1 0 0 0 1 1 0 1 0 0 1 0 1 0 0 0 0 0 0 0 0 0 0 1 0 0 1 1 1 0 0 0 0 1 0 0 0 1 1
// 0 0 1 0 0 0 1 0 0 0 0 1 1 0 0 1 0 1 1 1 1 1 1 1 1 0 1 1 1 1 1 1 0 1 1 1 0 0 1 1 0 0 1 1 1 0 0 0
// 1 0 1 1 0 1 1 1 0 1 0 1 1 1 1 1 1 1 1 0 1 0 1 1 1 0 1 1 1 1 1 1 1 1 1 0 1 1 1 1 0 0 0 1 1 0 1 1
void psi_ca_sender(ENCRYPTO::PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock,
                   sci::NetIO *ioArr[3], osuCrypto::Channel &chl, NetIO &io, NetIO &io2) {
  auto start = std::chrono::steady_clock::now();
  VOLEOPRF::PP pp;
  pp = VOLEOPRF::Setup(22);
  // std::vector<block> tmp;
  // for (auto i = 0; i < 33; i++) {
  //   tmp.emplace_back(Block::MakeBlock(0, i));
  // }
  // std::vector<uint8_t> oprf_key = VOLEOPRF::Server1(io, pp);
  // std::vector<block> vec_Fk_X = VOLEOPRF::Evaluate1(pp, oprf_key, tmp, pp.INPUT_NUM);

  // io.SendBlocks(vec_Fk_X.data(), pp.INPUT_NUM);

  // std::vector<block> numbers(16,Block::zero_block);
  // auto ans = perform_block_equality(numbers, context, sock, ioArr, chl);
  // for(auto i=0;i<ans.size();i++)if(ans[i]==1)std::cout<<"1 ";else std::cout<<"0 ";
  // prepare random set
  uint64_t nbins;
  io.ReceiveBytes(&nbins, 8);
  PRG::Seed seed = PRG::SetSeed(fixed_seed, 0);  // initialize PRG
  std::vector<uint64_t> vec;
  auto item_num = 1ull << 24;
  for (auto i = 0; i < item_num; i++) vec.emplace_back(i);
  auto random_values = PRG::GenRandomBlocks(seed, nbins);
  // for(auto i=0;i<nbins;i++){
  //   random_values[i]=Block::zero_block;
  // }
  // // oprf
  std::vector<uint8_t> oprf_key = VOLEOPRF::Server1(io, pp);

  // // prepare OKVS
  // std::vector<block> tmp = {Block::MakeBlock(0, 0xc)};
  // Block::PrintBlocks(oprf_evaluate(tmp, oprf_key));

  ENCRYPTO::SimpleTable simple_table(static_cast<std::size_t>(nbins));
  simple_table.SetNumOfHashFunctions(context.nfuns);
  simple_table.Insert(vec);
  simple_table.MapElements();
  // simple_table.Print();

  auto simple_table_size = simple_table.AsRaw2DVectorNoID();
  auto simple_table_vec = std::get<0>(simple_table_size);
  auto max_size = std::get<1>(simple_table_size);
  auto simple_table_1d = simple_table.AsRawVectorNoID();
  // std::vector<block> key_okvs;
  // key_okvs.reserve(context.nfuns * vec.size());
  // std::vector<block> val_okvs_2(context.nfuns * vec.size());
  std::vector<block> oprf_result =
      VOLEOPRF::Evaluate1(pp, oprf_key, simple_table_1d, simple_table_1d.size());
  auto tmp = 0;
  for (auto i = 0; i < simple_table_vec.size(); i++) {
    std::vector<block> &v = simple_table_vec[i];
    // key_okvs.insert(key_okvs.end(), v.begin(), v.end());
    // Block::PrintBlocks(simple_table_vec[i]);
    // std::cout << "----------------------" << i << "------------------------" << std::endl;
    // Block::PrintBlocks(oprf_result);
    for (auto j = 0; j < v.size(); j++, tmp++) {
      oprf_result[tmp]=(random_values[i] ^ oprf_result[tmp]);
    }
    if (i % 1000 == 0)
      std::cout << "----------------------" << i << "------------------------" << std::endl;
  }
  uint64_t baxos_size = vec.size() * context.nfuns;
  io.SendBytes(&baxos_size, 8);
  send_baxos(io,simple_table_1d,oprf_result,baxos_size);
  // Baxos<gf_128> baxos(baxos_size, 1 << 18, 3);
  // std::vector<block> encode_result(baxos.bin_num * baxos.total_size);
  // for(auto i=0;i<baxos_size;i++){
  //   std::cout<<i<<std::endl;
  //   Block::PrintBlock(key_okvs[i]);
  //   Block::PrintBlock(val_okvs[i]);
  // AES::FastECBEnc(pp.okvs.seed.aes_key, key_okvs.data(), key_okvs.size());
  // for (auto i = 0; i < key_okvs.size(); i++) {
  //   if (Block::Compare(key_okvs[i], Block::MakeBlock(0xbeac4722c5abd02f,0x386bce3b96428a0b))) {
  //     Block::PrintBlock(key_okvs[i]);
  //     std::cout << i << std::endl;
  //   }
  // }
  // for (int i = 0; i < key_okvs.size(); ++i) {
  //       bool isDuplicate = false;
  //       for (int j = i + 1; j < key_okvs.size(); ++j) {
  //           if (Block::Compare(key_okvs[i],key_okvs[j])) {
  //             Block::PrintBlock(key_okvs[i]);
  //             Block::PrintBlock(key_okvs[j]);
  //             std::cout<<"dumplacate keys at"<<i<<" "<<j<<std::endl;
  //           }
  //       }
  //   }
  // std::cout << "begin solve" << std::endl;
  // baxos.solve(simple_table_1d, oprf_result, encode_result, 0, 8);
  // baxos.decode(key_okvs,val_okvs_2,encode_result,8);
  // std::cout<<Block::Compare(val_okvs,val_okvs_2)<<std::endl;
  // // send OKVS
  // io.SendBlocks(encode_result.data(), encode_result.size());
  // // EQ
  std::cout << "begin eq:" << std::endl;
  // Block::PrintBlocks(random_values);
  auto ans = perform_block_equality(random_values, context, sock, ioArr, chl);
  for (auto i = 0; i < ans.size(); i++)
    if (ans[i] == 1)
      std::cout << "1 ";
    else
      std::cout << "0 ";
  std::cout << std::endl;

  // // auto pp_ot = ALSZOTE::Setup(BASE_LEN);
  // // std::vector<block> ot1(384,Block::all_one_block);
  // // std::vector<block> ot2(384,Block::all_one_block);
  // // ALSZOTE::Send(io, pp_ot, ot1, ot2, 384);

  auto ot_r = PRG::GenRandomBlocks(seed, nbins);
  std::vector<std::vector<block>> ot(2);
  ot[0].reserve(nbins);
  ot[1].reserve(nbins);
  auto one_block = Block::MakeBlock(0, 1);
  for (auto i = 0; i < nbins; i++) {
    ot[ans[i]].emplace_back(ot_r[i]);
    ot[1 - ans[i]].emplace_back(add_with_carry(ot_r[i], one_block));
  }
  for (auto i = 0; i < 128 - nbins % 128; i++) {
    ot[0].emplace_back(Block::zero_block);
    ot[1].emplace_back(Block::zero_block);
  }
  auto pp_ot = IKNPOTE::Setup(BASE_LEN);
  std::cout << nbins + (128 - nbins % 128) << " " << ot[0].size() << " " << ot[1].size()
            << std::endl;
  IKNPOTE::Send(io2, pp_ot, ot[0], ot[1], nbins + (128 - nbins % 128));
  block psi_ca_ans = Block::zero_block;
  for (auto i = 0; i < nbins; i++) {
    psi_ca_ans = add_with_carry(psi_ca_ans, ot[ans[i]][i]);
  }
  io2.SendBlock(psi_ca_ans);
  auto end = std::chrono::steady_clock::now();

  // 计算时间间隔
  std::chrono::duration<double> elapsed_seconds = end - start;
  std::cout << "Elapsed time: " << elapsed_seconds.count() << "s\n";
}

auto read_test_options(int32_t argcp, char **argvp) {
  namespace po = boost::program_options;
  ENCRYPTO::PsiAnalyticsContext context;
  po::options_description allowed("Allowed options");
  std::string type;
  // clang-format off
  allowed.add_options()("help,h", "produce this message")
  ("role,r",         po::value<decltype(context.role)>(&context.role)->required(),                                  "Role of the node")
  ("neles,n",        po::value<decltype(context.neles)>(&context.neles)->default_value(4096u),                      "Number of my elements")
  ("bit-length,b",   po::value<decltype(context.bitlen)>(&context.bitlen)->default_value(62u),                      "Bit-length of the elements")
  ("epsilon,e",      po::value<decltype(context.epsilon)>(&context.epsilon)->default_value(1.27f),                   "Epsilon, a table size multiplier")
  ("hint-epsilon,E",      po::value<decltype(context.fepsilon)>(&context.fepsilon)->default_value(1.27f),           "Epsilon, a hint table size multiplier")
  ("address,a",      po::value<decltype(context.address)>(&context.address)->default_value("127.0.0.1"),            "IP address of the server")
  ("port,p",         po::value<decltype(context.port)>(&context.port)->default_value(7777),                         "Port of the server")
  ("radix,m",    po::value<decltype(context.radix)>(&context.radix)->default_value(5u),                             "Radix in PSM Protocol")
  ("functions,f",    po::value<decltype(context.nfuns)>(&context.nfuns)->default_value(3u),                         "Number of hash functions in hash tables")
  ("hint-functions,F",    po::value<decltype(context.ffuns)>(&context.ffuns)->default_value(3u),                         "Number of hash functions in hint hash tables")
  ("psm-type,y",         po::value<std::string>(&type)->default_value("PSM1"),                                          "PSM type {PSM1, PSM2}");
  // clang-format on

  po::variables_map vm;
  try {
    po::store(po::parse_command_line(argcp, argvp, allowed), vm);
    po::notify(vm);
  } catch (const boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<
               boost::program_options::required_option>> &e) {
    if (!vm.count("help")) {
      std::cout << e.what() << std::endl;
      std::cout << allowed << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  if (vm.count("help")) {
    std::cout << allowed << "\n";
    exit(EXIT_SUCCESS);
  }

  if (type.compare("PSM1") == 0) {
    context.psm_type = ENCRYPTO::PsiAnalyticsContext::PSM1;
  } else if (type.compare("PSM2") == 0) {
    context.psm_type = ENCRYPTO::PsiAnalyticsContext::PSM2;
  } else {
    std::string error_msg(std::string("Unknown PSM type: " + type));
    throw std::runtime_error(error_msg.c_str());
  }

  context.nbins = context.neles * context.epsilon;

  context.fbins = context.fepsilon * context.neles * context.nfuns;

  return context;
}
void printFileContent(const std::string &filename) {
  // 打开文件
  std::ifstream infile(filename);

  // 检查文件是否成功打开
  if (!infile.is_open()) {
    std::cerr << "无法打开文件: " << filename << std::endl;
    return;
  }

  // 读取文件内容
  std::string line;
  std::cout << "内容 " << filename << ":" << std::endl;
  while (std::getline(infile, line)) {
    // 输出每一行内容
    std::cout << line << std::endl;
  }

  // 关闭文件
  infile.close();
  std::cout << std::endl;
}
int main(int argc, char **argv) {
  // test_baxos_block();
  // return 0;
  CRYPTO_Initialize();
  // test_baxos_block();
  // baxos.decode(key_set, decode_result, encode_result, thread_num);
  // std::cout<<"over"<<std::endl;

  auto context = read_test_options(argc, argv);
  auto gen_bitlen = static_cast<std::size_t>(std::ceil(std::log2(context.neles))) + 3;
  std::vector<uint64_t> inputs;

  if (context.role == CLIENT) {
    for (int i = 0; i < context.neles; i++) {
      inputs.push_back(1000 * i);
    }
  } else {
    for (int i = 0; i < context.neles; i++) {
      inputs.push_back(2000 * i);
    }
  }

  // Setup Connection
  std::unique_ptr<CSocket> sock = ENCRYPTO::EstablishConnection(context.address, context.port,
                                                                static_cast<e_role>(context.role));
  sci::NetIO *ioArr[3];
  osuCrypto::IOService ios;
  osuCrypto::Channel chl;
  osuCrypto::Session *ep;
  std::string name = "n";
  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(20);
  std::vector<block> tmp;

  for (auto i = 0; i < 128; i++) {
    tmp.emplace_back(Block::MakeBlock(0, i));
  }
  if (context.role == SERVER) {
    NetIO io("server", "", 8080);
    NetIO io2("server", "", 8081);

    ioArr[0] = new sci::NetIO(nullptr, context.port + 1);
    ioArr[1] = new sci::NetIO(nullptr, context.port + 2);
    ioArr[2] = new sci::NetIO(nullptr, context.port + 3);
    ep = new osuCrypto::Session(ios, context.address, context.port + 4,
                                osuCrypto::SessionMode::Server, name);
    chl = ep->addChannel(name, name);
    psi_ca_sender(context, sock, ioArr, chl, io, io2);
  } else {
    NetIO io("client", "127.0.0.1", 8080);
    NetIO io2("client", "127.0.0.1", 8081);

    ioArr[0] = new sci::NetIO(context.address.c_str(), context.port + 1);
    ioArr[1] = new sci::NetIO(context.address.c_str(), context.port + 2);
    ioArr[2] = new sci::NetIO(context.address.c_str(), context.port + 3);
    ep = new osuCrypto::Session(ios, context.address, context.port + 4,
                                osuCrypto::SessionMode::Client, name);
    chl = ep->addChannel(name, name);
    PrintSplitLine('-');
    psi_ca_receiver(context, sock, ioArr, chl, io, io2);
  }

  // run_eq(inputs, context, sock, ioArr, chl);
  // ResetCommunication(sock, chl, ioArr, context);
  // run_circuit_psi(inputs, context, sock, ioArr, chl);
  // PrintTimings(context);
  // AccumulateCommunicationPSI(sock, chl, ioArr, context);
  // PrintCommunication(context);

  // End Connection

  // printFileContent("res_share_P0.dat");
  // printFileContent("res_share_P1.dat");

  sock->Close();
  chl.close();
  ep->stop();
  ios.stop();

  for (int i = 0; i < 3; i++) {
    delete ioArr[i];
  }
}
