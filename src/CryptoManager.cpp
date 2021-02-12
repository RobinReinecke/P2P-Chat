#include "CryptoManager.h"
#include "Helper.h"
#include <iostream>
#include <openssl/pem.h>
#include <openssl/aes.h>
#include <algorithm>

CryptoManager::CryptoManager(const std::string &hostname) {
    generateKeyPair(hostname);

    // init aes
    aesEncryptContext = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_init(aesEncryptContext);
    EVP_CipherInit_ex(aesEncryptContext, EVP_aes_256_cbc(), nullptr, nullptr, nullptr, 1);
    aesKeyLength = EVP_CIPHER_CTX_key_length(aesEncryptContext);
    aesIvLength = EVP_CIPHER_CTX_iv_length(aesEncryptContext);
    aesDecryptContext = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_init(aesDecryptContext);

    // init rsa
    rsaEncryptContext = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_init(rsaEncryptContext);
    rsaDecryptContext = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_init(rsaDecryptContext);
}

/**
 * Get public key for a given hostname.
 * @param hostname
 * @return public key or empty string if hostname is unknown
 */
std::string CryptoManager::get(const std::string &hostname) const {
    auto publicKey = publicKeys.find(hostname);

    if (publicKey == publicKeys.end()) return "";
    return publicKey->second;
}

/**
 * Add a new pair of hostname and public key.
 * @param hostname
 * @param publicKey
 * @return true if successful
 */
bool CryptoManager::add(const std::string &hostname, const std::string &publicKey) {
    return publicKeys.emplace(hostname, publicKey).second;
}

/**
 * Remove the hostname.
 * @param hostname
 * @return true if successful
 */
bool CryptoManager::remove(const std::string &hostname) {
    return publicKeys.erase(hostname) > 0;
}

/**
 * Save or override a key for the groupname.
 * @param groupName
 * @param key
 * @return true if successful
 */
bool CryptoManager::setGroupKey(const std::string &groupName, const std::string &key) {
    // remove maybe old existing group keys
    groupKeys.erase(groupName);

    auto *aesKey = (unsigned char *) malloc(aesKeyLength);
    auto *aesIV = (unsigned char *) malloc(aesIvLength);

    auto *aesPass = (unsigned char *) malloc(aesKeyLength);
    memset(aesPass, 0, aesKeyLength);
    auto *aesSalt = (unsigned char *) malloc(8);
    memset(aesSalt, 0, aesIvLength);

    strncpy((char *) aesPass, key.c_str(), std::min(key.length(), aesKeyLength));
    strncpy((char *) aesSalt, key.c_str(), std::min((int) key.length(), 8));

    if (EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha256(), aesSalt, aesPass, aesKeyLength, 6, aesKey, aesIV) == 0) {
        return false;
    }
    free(aesPass);
    free(aesSalt);

    return groupKeys.emplace(groupName, std::make_pair(aesKey, aesIV)).second;
}

/**
 * Load data from json.
 * @param json
 */
void CryptoManager::loadJson(const json &json) {
    for (const auto &element: json.items()) {
        add(element.value()[0], element.value()[1]);
    }
}

/**
 * Convert current state to json. Group keys are not converted.
 * @return json of data
 */
json CryptoManager::toJson() {
    json j;
    for (const auto &element: publicKeys) {
        j.push_back({element.first, element.second});
    }
    return j;
}

/**
 * Encrypt the plaintext with the public key of the passed target hostname.
 * @param plaintext
 * @param target hostname of the target peer
 * @return encrypted string or empty string on error
 */
std::string CryptoManager::publicEncrypt(const std::string &plaintext, const std::string &target) {
    // get remote public key
    std::string publicKeyString = get(target);
    const char *publicKeyChar = publicKeyString.c_str();
    BIO *publicBIO = BIO_new_mem_buf(publicKeyChar, -1);
    EVP_PKEY *remotePubKey = PEM_read_bio_PUBKEY(publicBIO, nullptr, nullptr, nullptr);
    BIO_free_all(publicBIO);

    // init
    size_t encMsgLen = 0;
    size_t blockLen = 0;
    auto *ek = (unsigned char *) malloc(EVP_PKEY_size(remotePubKey));
    auto *iv = (unsigned char *) malloc(EVP_MAX_IV_LENGTH);

    size_t ekl = 0;
    size_t ivl = EVP_MAX_IV_LENGTH;
    size_t msgLen = plaintext.size() + 1;
    auto *encMsg = (unsigned char *) malloc(msgLen + EVP_MAX_IV_LENGTH);

    if (!EVP_SealInit(rsaEncryptContext, EVP_aes_256_cbc(), &ek, reinterpret_cast<int *>(&ekl), iv, &remotePubKey, 1)) {
        return std::string();
    }

    // encrypt
    if (!EVP_SealUpdate(rsaEncryptContext, encMsg + encMsgLen, (int *) &blockLen,
                        (const unsigned char *) plaintext.c_str(),(int) msgLen)) {
        return std::string();
    }
    encMsgLen += blockLen;

    if (!EVP_SealFinal(rsaEncryptContext, encMsg + encMsgLen, (int *) &blockLen)) {
        return std::string();
    }
    encMsgLen += blockLen;

    return std::string(base64Encode(ek, ekl)) + "#" + std::to_string(ekl) + "#" + std::string(base64Encode(iv, ivl)) +
           "#" + std::to_string(ivl) + "#" +
           std::string(base64Encode(encMsg, encMsgLen)) + "#" + std::to_string(encMsgLen);
}

/**
 * Decrypt the passed encrypted string with the local private key.
 * @param encyptedText
 * @return plaintext or empty string on error
 */
std::string CryptoManager::privateDecrypt(const std::string &encryptedText) {
    auto tokens = split(encryptedText, '#');
    unsigned char *ek, *iv, *encMsg;
    base64Decode(tokens[0].c_str(), tokens[0].length(), &ek);
    base64Decode(tokens[2].c_str(), tokens[2].length(), &iv);
    base64Decode(tokens[4].c_str(), tokens[4].length(), &encMsg);
    size_t encMsgLen = std::stoi(tokens[5]);
    size_t ekl = std::stoi(tokens[1]); //ek length
    size_t ivl = std::stoi(tokens[3]); //iv length

    // get private key
    const char *privateKeyChar = privateKey.c_str();
    BIO *privateBIO = BIO_new_mem_buf(privateKeyChar, -1);
    EVP_PKEY *privKey = PEM_read_bio_PrivateKey(privateBIO, nullptr, nullptr, nullptr);
    BIO_free_all(privateBIO);

    // init
    size_t decLen = 0;
    size_t blockLen = 0;
    auto *decMsg = (unsigned char *) malloc(encMsgLen + ivl);

    if (!EVP_OpenInit(rsaDecryptContext, EVP_aes_256_cbc(), ek, ekl, iv, privKey)) {
        return std::string();
    }

    // decrypt
    if (!EVP_OpenUpdate(rsaDecryptContext, decMsg, (int *) &blockLen, encMsg, (int) encMsgLen)) {
        return std::string();
    }
    decLen += blockLen;

    if (!EVP_OpenFinal(rsaDecryptContext, decMsg + decLen, reinterpret_cast<int *>(&blockLen))) {
        return std::string();
    }
    decLen += blockLen;

    return std::string(reinterpret_cast<char *>(decMsg), decLen);
}

/**
 * Encrypt the plaintext with the key of the passed group name.
 * @param plaintext
 * @param groupName
 * @return
 */
std::string CryptoManager::groupEncrypt(const std::string &plaintext, const std::string &groupName) {
    auto groupKey = groupKeys.find(groupName)->second;
    size_t blockLength = 0;
    size_t encryptedMessageLength = 0;
    size_t msgLen = plaintext.size() + 1;

    auto *encryptedMessage = (unsigned char *) malloc(msgLen + AES_BLOCK_SIZE);
    if (encryptedMessage == nullptr) {
        return std::string();
    }

    // Encrypt it!
    if (!EVP_EncryptInit_ex(aesEncryptContext, EVP_aes_256_cbc(), nullptr, groupKey.first, groupKey.second)) {
        return std::string();
    }

    if (!EVP_EncryptUpdate(aesEncryptContext, encryptedMessage, (int *) &blockLength,
                           (unsigned char *) plaintext.c_str(), (int) msgLen)) {
        return std::string();
    }
    encryptedMessageLength += blockLength;

    if (!EVP_EncryptFinal_ex(aesEncryptContext, encryptedMessage + encryptedMessageLength, (int *) &blockLength)) {
        return std::string();
    }

    return std::string(base64Encode(encryptedMessage, encryptedMessageLength + blockLength)) + "#" +
           std::to_string(encryptedMessageLength + blockLength);
}

/**
 * Decrypt the passed encrypted string with the key of the passed group.
 * @param encyptedText
 * @return plaintext or empty string on error
 */
std::string CryptoManager::groupDecrypt(const std::string &encryptedText, const std::string &groupName) {
    auto groupKey = groupKeys.find(groupName)->second;
    unsigned char *encryptedMessage;
    size_t decryptedMessageLength = 0;
    size_t blockLength = 0;

    auto tokens = split(encryptedText, '#');
    base64Decode(tokens[0].c_str(), tokens[0].length(), &encryptedMessage);
    size_t encryptedMessageLength = std::stoi(tokens[1]);

    auto decryptedMessage = (unsigned char *) malloc(encryptedMessageLength);
    if (decryptedMessage == nullptr) {
        return std::string();
    }

    // Decrypt it!
    if (!EVP_DecryptInit_ex(aesDecryptContext, EVP_aes_256_cbc(), nullptr, groupKey.first, groupKey.second)) {
        return std::string();
    }

    if (!EVP_DecryptUpdate(aesDecryptContext, decryptedMessage, (int *) &blockLength, encryptedMessage,
                           (int) encryptedMessageLength)) {
        return std::string();
    }
    decryptedMessageLength += blockLength;

    if (!EVP_DecryptFinal_ex(aesDecryptContext, decryptedMessage + decryptedMessageLength, (int *) &blockLength)) {
        return std::string();
    }
    decryptedMessageLength += blockLength;

    return std::string(reinterpret_cast<char *>(decryptedMessage), decryptedMessageLength);
}

/**
 * Generate a keypair on initialization and save it.
 * @param hostname The hostname the public key is associated to
 */
void CryptoManager::generateKeyPair(const std::string &hostname) {
    // Init RSA
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        return;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, RSA_KEYLEN) <= 0) {
        return;
    }

    // generate keypair
    EVP_PKEY *localKeypair = nullptr;
    if (EVP_PKEY_keygen(ctx, &localKeypair) <= 0) {
        return;
    }

    EVP_PKEY_CTX_free(ctx);

    // save private key string
    BIO *privateBIO = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(privateBIO, localKeypair, nullptr, nullptr, 0, 0, nullptr);
    int privateKeyLen = BIO_pending(privateBIO);
    char *privateKeyChar = (char *) malloc(privateKeyLen);
    BIO_read(privateBIO, privateKeyChar, privateKeyLen);
    BIO_free_all(privateBIO);
    privateKey = privateKeyChar;

    // save public key string
    BIO *publicBIO = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(publicBIO, localKeypair);
    int publicKeyLen = BIO_pending(publicBIO);
    char *publicKeyChar = (char *) malloc(publicKeyLen);
    BIO_read(publicBIO, publicKeyChar, publicKeyLen);
    BIO_free_all(publicBIO);
    add(hostname, publicKeyChar);
}
