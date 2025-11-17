#ifndef SHA256_H
#define SHA256_H

#include <openssl/evp.h>

#define SHA256_DIGEST_LENGTH 32
#define SHA256_BLOCK_SIZE 32

void generate_sha256_key(unsigned char *key);
void generate_nonce(unsigned char *nonce);
int parse_sha256_key(const char *key_str, unsigned char *key);
int send_sha256_key(int sock, const unsigned char *key);
int receive_sha256_key(int sock, unsigned char *key);
int verify_sha256_key(const unsigned char *local_key, const unsigned char *received_key);
void hash_data(const unsigned char *data, size_t data_len, unsigned char *hash);
void hash_data_with_nonce(const unsigned char *data, size_t data_len, const unsigned char *nonce, size_t nonce_len, unsigned char *hash);

#endif /* SHA256_H */

