#include "sha256.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


void hash_data(const unsigned char *data, size_t data_len, unsigned char *hash) {
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned int md_len;

    md = EVP_sha256();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, data, data_len);
    EVP_DigestFinal_ex(mdctx, hash, &md_len);
    EVP_MD_CTX_free(mdctx);
}

void generate_nonce(unsigned char *nonce) {
    FILE *rand_file = fopen("/dev/urandom", "r");
    if (rand_file == NULL) {
        perror("Failed to open /dev/urandom for nonce generation");
        exit(1);
    }
    if (fread(nonce, 1, SHA256_DIGEST_LENGTH, rand_file) != SHA256_DIGEST_LENGTH) {
        perror("Failed to read enough random bytes for nonce");
        exit(1);
    }
    fclose(rand_file);
}

void hash_data_with_nonce(const unsigned char *data, size_t data_len, const unsigned char *nonce, size_t nonce_len, unsigned char *hash) {
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;
    unsigned int md_len;

    md = EVP_sha256();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, data, data_len);
    EVP_DigestUpdate(mdctx, nonce, nonce_len);
    EVP_DigestFinal_ex(mdctx, hash, &md_len);
    EVP_MD_CTX_free(mdctx);
}

void generate_sha256_key(unsigned char *key) {
    /* Generate a random SHA-256 key */
    unsigned char random_data[SHA256_BLOCK_SIZE];
    FILE *rand_file = fopen("/dev/urandom", "r");
    if (rand_file == NULL) {
        perror("Failed to open /dev/urandom");
        exit(1);
    }
    if (fread(random_data, 1, sizeof(random_data), rand_file) != sizeof(random_data)) {
        perror("Failed to read enough random bytes for key generation");
        fclose(rand_file);
        exit(1);
    }
    fclose(rand_file);
    hash_data(random_data, sizeof(random_data), key);
}

int parse_sha256_key(const char *key_str, unsigned char *key) {
    /* Parse the provided SHA-256 key from a string */
    if (strlen(key_str) != SHA256_DIGEST_LENGTH * 2) {
        fprintf(stderr, "Invalid SHA-256 key length. Expected %d characters.\n", SHA256_DIGEST_LENGTH * 2);
        return -1;
    }
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sscanf(key_str + (i * 2), "%2hhx", &key[i]);
    }
    return 0;
}

int send_sha256_key(int sock, const unsigned char *key) {
    /* Send the SHA-256 key over a socket */
    ssize_t total_sent = 0;
    ssize_t bytes_left = SHA256_DIGEST_LENGTH;
    const unsigned char *ptr = key;

    //fprintf(stderr, "send_sha256_key: Attempting to send %u bytes.\n", SHA256_DIGEST_LENGTH);

    while (total_sent < SHA256_DIGEST_LENGTH) {
        ssize_t sent = write(sock, ptr + total_sent, bytes_left);
        if (sent == -1) {
            perror("Failed to send SHA-256 key");
            return -1;
        }
        //fprintf(stderr, "send_sha256_key: Sent %zd bytes, total sent %zd.\n", sent, total_sent + sent);
        total_sent += sent;
        bytes_left -= sent;
    }
    //fprintf(stderr, "send_sha256_key: Successfully sent %zd bytes.\n", total_sent);
    return 0;
}

int receive_sha256_key(int sock, unsigned char *key) {
    /* Receive the SHA-256 key over a socket */
    ssize_t total_received = 0;
    ssize_t bytes_left = SHA256_DIGEST_LENGTH;
    unsigned char *ptr = key;

    //fprintf(stderr, "receive_sha256_key: Attempting to receive %u bytes.\n", SHA256_DIGEST_LENGTH);

    while (total_received < SHA256_DIGEST_LENGTH) {
        ssize_t received = read(sock, ptr + total_received, bytes_left);
        if (received == -1) {
            perror("Failed to receive SHA-256 key");
            return -1;
        }
        if (received == 0) {
            //fprintf(stderr, "receive_sha256_key: Connection closed while receiving SHA-256 key.\n");
            return -1; // Connection closed prematurely
        }
        //fprintf(stderr, "receive_sha256_key: Received %zd bytes, total received %zd.\n", received, total_received + received);
        total_received += received;
        bytes_left -= received;
    }
    //fprintf(stderr, "receive_sha256_key: Successfully received %zd bytes.\n", total_received);
    return 0;
}

int verify_sha256_key(const unsigned char *local_key, const unsigned char *received_key) {
    /* Verify that the local and received keys match */
    return (memcmp(local_key, received_key, SHA256_DIGEST_LENGTH) == 0);
}
