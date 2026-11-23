#pragma once
// frp client (frpc) — control connection + yamux streams + UDP proxy to the local
// RakNet server. Port of pocketmine\frp\frpc.php.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct evp_cipher_st;
typedef struct evp_cipher_st EVP_CIPHER;
struct evp_cipher_ctx_st;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;

namespace mpmpes::frp {

struct FrpProxy {
  std::string name;
  std::string type = "udp";
  int local_port = 0;
  int remote_port = 0;
  std::string local_ip = "127.0.0.1";
};
struct FrpConfig {
  std::string server_addr;
  int server_port = 7000;
  std::string user;
  std::string token;
  std::vector<FrpProxy> proxies;
};
// parse frpc TOML (serverAddr/serverPort/[auth]/[[proxies]])
bool loadFrpConfig(const std::string& path, FrpConfig& out);

// AES-128-CFB keystream (PHP openssl aes-128-ecb of prev block)
class AesCfb {
public:
  explicit AesCfb(std::string key, std::string iv);
  ~AesCfb();
  std::string process(std::string_view data);
private:
  EVP_CIPHER_CTX* ctx_ = nullptr;
  std::string prev_;
};

class FrpClient {
public:
  FrpClient(std::string tunnel_name, FrpConfig conf);
  ~FrpClient();
  bool start();
  void stop();
  bool running() const { return running_; }

private:
  void run();
  void runImpl();
  void stopImpl();

  std::string tunnel_name_;
  FrpConfig conf_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  int control_fd_ = -1;
  std::string run_id_;
};

} // namespace mpmpes::frp
