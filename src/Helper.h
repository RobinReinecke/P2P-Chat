#ifndef HELPER_H
#define HELPER_H

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <nlohmann/json.hpp>
#include <openssl/pem.h>

using json = nlohmann::json;

/**
 * Trim from start.
 * @param s String to trim
 */
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

/**
 * Trim from end.
 * @param s String to trim
 */
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(),
            s.end());
}

/**
 * Trim from both ends
 * @param s String to trim
 */
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

/**
 * Trim from start.
 * @param s String to trim
 * @return Trimmed copy of s
 */
static inline std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

/**
 * Trim from end.
 * @param s String to trim
 * @return Trimmed copy of s
 */
static inline std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

/**
 * Trim from both ends.
 * @param s String to trim
 * @return Trimmed copy of s
 */
static inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

/**
 * Try to parse a string into a json.
 * @param buffer json string
 * @return nullptr if parsing failed
 */
static inline json tryParse(const std::string &buffer) {
    try {
        return json::parse(buffer);
    }
    catch (nlohmann::detail::parse_error &ex) {
    }
    return nullptr;
}

static inline std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

static inline char *base64Encode(const unsigned char *message, const size_t length) {
    int encodedSize = 4 * ceil((double) length / 3);
    char *b64text = (char *) malloc(encodedSize + 1);

    if (b64text == nullptr) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(1);
    }

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    BIO_write(bio, message, length);
    BIO_flush(bio);

    BUF_MEM *bufferPtr;
    BIO_get_mem_ptr(bio, &bufferPtr);
    BIO_set_close(bio, BIO_CLOSE);

    memcpy(b64text, (*bufferPtr).data, (*bufferPtr).length + 1);
    b64text[(*bufferPtr).length] = '\0';

    BIO_free_all(bio);
    return b64text;
}

static inline int calcDecodeLength(const char *b64input, const size_t length) {
    unsigned int padding = 0;

    // Check for trailing '=''s as padding
    if (b64input[length - 1] == '=' && b64input[length - 2] == '=') {
        padding = 2;
    } else if (b64input[length - 1] == '=') {
        padding = 1;
    }

    return (int) length * 0.75 - padding;
}

static inline int base64Decode(const char *b64message, const size_t length, unsigned char **buffer) {
    int decodedLength = calcDecodeLength(b64message, length);
    *buffer = (unsigned char *) malloc(decodedLength + 1);

    if (*buffer == nullptr) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(1);
    }

    BIO *bio = BIO_new_mem_buf(b64message, -1);
    BIO *b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    decodedLength = BIO_read(bio, *buffer, strlen(b64message));
    (*buffer)[decodedLength] = '\0';

    BIO_free_all(bio);

    return decodedLength;
}

#endif
