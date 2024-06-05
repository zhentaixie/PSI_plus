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
#include "../../Kunlun/crypto/setup.hpp"
#include "../../Kunlun/mpc/oprf/vole_oprf.hpp"
#include "ENCRYPTO_utils/connection.h"
#include "ENCRYPTO_utils/socket.h"
#include "HashingTables/common/hash_table_entry.h"
#include "HashingTables/common/hashing.h"
#include "HashingTables/cuckoo_hashing/cuckoo_hashing.h"
#include "HashingTables/simple_hashing/simple_hashing.h"
#include "abycore/aby/abyparty.h"
#include "common/config.h"
#include "common/functionalities.h"
std::vector<uint8_t> oprf_server(NetIO &io) {
  CRYPTO_Initialize();

  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(14);  // 40 is the statistical parameter

  std::vector<uint8_t> oprf_key = VOLEOPRF::Server1(io, pp);

  return oprf_key;
}
std::vector<block> oprf_evaluate(std::vector<block> vec, std::vector<uint8_t> oprf_key) {
  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(14);  // 40 is the statistical parameter
  auto vec_Fk_X = VOLEOPRF::Evaluate1(pp, oprf_key, vec, vec.size());
  return vec_Fk_X;
}
std::vector<block> oprf_client(std::vector<block> vec, NetIO &io) {
  CRYPTO_Initialize();

  VOLEOPRF::PP pp;

  pp = VOLEOPRF::Setup(14);  // 40 is the statistical parameter

  std::vector<block> vec_Fk_Y = VOLEOPRF::Client1(io, pp, vec, pp.INPUT_NUM);
  return vec_Fk_Y;
}

void psi_ca_receiver(ENCRYPTO::PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock,
                     sci::NetIO *ioArr[2], osuCrypto::Channel &chl, NetIO &io) {
                      
  // std::vector<__m128i> numbers;
  // for (auto i = 0; i < 8; i++) numbers.push_back(Block::zero_block);
  // numbers[0]=Block::all_one_block;
  // auto ans = perform_block_equality(numbers, context, sock, ioArr, chl);
  // for(auto i=0;i<ans.size();i++)if(ans[i]==1)std::cout<<"1 ";else std::cout<<"0 ";
  PRG::Seed seed = PRG::SetSeed();  // initialize PRG
  std::vector<uint64_t> vec = {1, 2, 3, 4, 5, 6,7,8,9,10,11,12,13,14};
  int nbins=16;
  ENCRYPTO::CuckooTable cuckoo_table(static_cast<std::size_t>(nbins));
  cuckoo_table.SetNumOfHashFunctions(context.nfuns);
  cuckoo_table.Insert(vec);
  cuckoo_table.MapElements();
  auto add = cuckoo_table.GetElementAddresses();
  if (cuckoo_table.GetStashSize() > 0u) {
    std::cerr << "[Error] Stash of size " << cuckoo_table.GetStashSize() << " occured\n";
  }
  auto idx_cuckoo_table = cuckoo_table.AsRawVectorNoID();
  auto idxs=std::get<0>(idx_cuckoo_table);
  auto cuckoo_table_v=std::get<1>(idx_cuckoo_table);
  // oprf
  auto result = oprf_client(cuckoo_table_v, io);

  // receive OKVS
  Baxos<gf_128> baxos(3*14, 1 << 14, 3);
  std::vector<block> okvs(baxos.total_size * baxos.bin_num, Block::zero_block);
  // io.ReceiveBlocks(okvs.data(), okvs.size());
  // std::cout<<"begin eq:"<<std::endl;
  // std::vector<block> decode_result(cuckoo_table_v.size());
  // baxos.decode(cuckoo_table_v, decode_result, okvs, 8);

  // // // decode OKVS

  // for (auto i = 0; i < vec.size(); i++) {
  //   decode_result[i] ^= result[i];
  // }

  std::vector<block> eq_blocks(nbins,Block::zero_block);
  // for(auto i=0;i<idxs.size();i++)eq_blocks[idxs[i]]=decode_result[i];
  auto ans=perform_block_equality(eq_blocks,context,sock,ioArr,chl);
  for(auto i=0;i<ans.size();i++)if(ans[i]==1)std::cout<<"1 ";else std::cout<<"0 ";
  std::cout<<std::endl;
  for(auto i=0;i<idxs.size();i++)std::cout<<idxs[i]<<" ";
  std::cout<<std::endl;
}

void psi_ca_sender(ENCRYPTO::PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock,
                   sci::NetIO *ioArr[2], osuCrypto::Channel &chl, NetIO &io) {
  // std::vector<block> numbers(8,Block::zero_block);
  // auto ans = perform_block_equality(numbers, context, sock, ioArr, chl);
  // for(auto i=0;i<ans.size();i++)if(ans[i]==1)std::cout<<"1 ";else std::cout<<"0 ";
  // prepare random set
  // int LOG_INPUT_NUM = 20;
  int nbins = 16;
  PRG::Seed seed = PRG::SetSeed(fixed_seed, 0);  // initialize PRG
  std::vector<uint64_t> vec = {1, 2, 3, 4, 5, 6,7,8,9,10,11,12,13,14};
  auto random_values = PRG::GenRandomBlocks(seed, nbins);

  // oprf
  auto oprf_key = oprf_server(io);
  // prepare OKVS

  ENCRYPTO::SimpleTable simple_table(static_cast<std::size_t>(nbins));
  simple_table.SetNumOfHashFunctions(context.nfuns);
  simple_table.Insert(vec);
  simple_table.MapElements();
  // simple_table.Print();

  auto simple_table_size = simple_table.AsRaw2DVectorNoID();
  auto simple_table_vec = std::get<0>(simple_table_size);
  auto max_size = std::get<1>(simple_table_size);
  std::vector<block> key_okvs;
  key_okvs.reserve(context.nfuns * vec.size());
  std::vector<block> val_okvs;

  for (auto i = 0; i < simple_table_vec.size(); i++) {
    std::vector<block> &v = simple_table_vec[i];
    key_okvs.insert(key_okvs.end(), v.begin(),v.end());
    auto oprf_result = oprf_evaluate(v, oprf_key);
    for (auto j = 0; j < v.size(); j++) {
      val_okvs.emplace_back(random_values[i] ^ oprf_result[j]);
    }
  }

  Baxos<gf_128> baxos(vec.size() * context.nfuns, 1 << 14, 3);
  std::vector<block> encode_result(baxos.bin_num * baxos.total_size);
  baxos.solve(key_okvs, val_okvs, encode_result, &seed, 8);

  // // send OKVS
  // io.SendBlocks(encode_result.data(), encode_result.size());
  // EQ
  std::cout<<"begin eq:"<<std::endl;

  auto ans=perform_block_equality(std::vector<block>(16,Block::zero_block),context,sock,ioArr,chl);
  for(auto i=0;i<ans.size();i++)if(ans[i]==1)std::cout<<"1 ";else std::cout<<"0 ";
  std::cout<<std::endl;
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
               boost::program_options::required_option> > &e) {
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
  sci::NetIO *ioArr[2];
  osuCrypto::IOService ios;
  osuCrypto::Channel chl;
  osuCrypto::Session *ep;
  std::string name = "n";

  if (context.role == SERVER) {
    NetIO io("server", "", 8080);

    ioArr[0] = new sci::NetIO(nullptr, context.port + 1);
    ioArr[1] = new sci::NetIO(nullptr, context.port + 2);
    ep = new osuCrypto::Session(ios, context.address, context.port + 3,
                                osuCrypto::SessionMode::Server, name);
    chl = ep->addChannel(name, name);
    psi_ca_sender(context, sock, ioArr, chl, io);
  } else {
    NetIO io("client", "127.0.0.1", 8080);

    ioArr[0] = new sci::NetIO(context.address.c_str(), context.port + 1);
    ioArr[1] = new sci::NetIO(context.address.c_str(), context.port + 2);
    ep = new osuCrypto::Session(ios, context.address, context.port + 3,
                                osuCrypto::SessionMode::Client, name);
    chl = ep->addChannel(name, name);
    psi_ca_receiver(context, sock, ioArr, chl, io);
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

  for (int i = 0; i < 2; i++) {
    delete ioArr[i];
  }
  return EXIT_SUCCESS;
}
