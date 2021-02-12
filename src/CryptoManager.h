#ifndef CRYPTOMANAGER_H
#define CRYPTOMANAGER_H

#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

using json = nlohmann::json;

#define RSA_KEYLEN 2048

class CryptoManager {
public:
    explicit CryptoManager(const std::string &hostname);

    std::string publicEncrypt(const std::string &plaintext, const std::string &target);
    std::string privateDecrypt(const std::string &encryptedText);
    std::string groupEncrypt(const std::string &plaintext, const std::string &groupName);
    std::string groupDecrypt(const std::string &encryptedText, const std::string &groupName);

    std::string get(const std::string &hostname) const;
    bool add(const std::string &hostname, const std::string &publicKey);
    bool remove(const std::string &hostname);
    bool setGroupKey(const std::string &groupName, const std::string &key);
    void loadJson(const json &json);
    json toJson();

    // getter & setter
    const std::string &getPrivateKey() const { return privateKey; };

private:
    std::map<std::string, std::string> publicKeys;
    std::map<std::string, std::pair<unsigned char *, unsigned char *>> groupKeys;
    std::string privateKey;
    // used for aes
    EVP_CIPHER_CTX *aesEncryptContext;
    EVP_CIPHER_CTX *aesDecryptContext;
    size_t aesKeyLength;
    size_t aesIvLength;
    // used for rsa
    EVP_CIPHER_CTX *rsaEncryptContext;
    EVP_CIPHER_CTX *rsaDecryptContext;

    void generateKeyPair(const std::string &hostname);
};

#endif
