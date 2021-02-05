/*
 * Copyright 2021 Benjamin Edgington
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../inc/acutest.h"
#include "debug_util.h"
#include "kzg_proofs.h"

// The generator for our "trusted" setup
blst_scalar secret =
    {
     0xa4, 0x73, 0x31, 0x95, 0x28, 0xc8, 0xb6, 0xea,
     0x4d, 0x08, 0xcc, 0x53, 0x18, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    }; // Little-endian?

void generate_setup(blst_p1 *s1, blst_p2 *s2, const blst_scalar *secret, const uint64_t n) {
    blst_fr s_pow, s;
    blst_fr_from_scalar(&s, secret);
    s_pow = fr_one;
    for (uint64_t i = 0; i < n; i++) {
        p1_mul(&s1[i], blst_p1_generator(), &s_pow);
        p2_mul(&s2[i], blst_p2_generator(), &s_pow);
        blst_fr_mul(&s_pow, &s_pow, &s);
    }
}

void title(void) {;}

void proof_single(void) {
    // Our polynomial: degree 15, 16 coefficients
    uint64_t coeffs[] = {1, 2, 3, 4, 7, 7, 7, 7, 13, 13, 13, 13, 13, 13, 13, 13};
    int poly_len = sizeof coeffs / sizeof coeffs[0];
    uint64_t secrets_len = poly_len + 1;

    FFTSettings fs;
    KZGSettings ks;
    poly p;
    blst_p1 commitment, proof;
    blst_p1 *s1 = malloc(secrets_len * sizeof(blst_p1));
    blst_p2 *s2 = malloc(secrets_len * sizeof(blst_p2));
    blst_fr x, value;

    // Create the polynomial
    init_poly(&p, poly_len);
    for (int i = 0; i < poly_len; i++) {
        fr_from_uint64(&p.coeffs[i], coeffs[i]);
    }

    // Initialise the secrets and data structures
    generate_setup(s1, s2, &secret, secrets_len);
    TEST_CHECK(C_KZG_OK == new_fft_settings(&fs, 4)); // ln_2 of poly_len
    TEST_CHECK(C_KZG_OK == new_kzg_settings(&ks, &fs, s1, s2, secrets_len));

    // Compute the proof for x = 25
    fr_from_uint64(&x, 25);
    commit_to_poly(&commitment, &ks, &p);
    TEST_CHECK(C_KZG_OK == compute_proof_single(&proof, &ks, &p, &x));

    // Verify the proof for x = 25
    eval_poly(&value, &p, &x);
    TEST_CHECK(true == check_proof_single(&ks, &commitment, &proof, &x, &value));

    free_fft_settings(&fs);
    free_poly(p);
    free(s1);
    free(s2);
}

void proof_multi(void) {
    // Our polynomial: degree 15, 16 coefficients
    uint64_t coeffs[] = {1, 2, 3, 4, 7, 7, 7, 7, 13, 13, 13, 13, 13, 13, 13, 13};
    int poly_len = sizeof coeffs / sizeof coeffs[0];
    uint64_t secrets_len = poly_len + 1;

    FFTSettings fs1, fs2;
    KZGSettings ks1, ks2;
    poly p;
    blst_p1 commitment, proof;
    blst_p1 *s1 = malloc(secrets_len * sizeof(blst_p1));
    blst_p2 *s2 = malloc(secrets_len * sizeof(blst_p2));
    blst_fr x, tmp;
    int coset_scale = 3, coset_len = (1 << coset_scale); // Where do these come from?
    blst_fr ys[coset_len];

    // Create the polynomial
    init_poly(&p, poly_len);
    for (int i = 0; i < poly_len; i++) {
        fr_from_uint64(&p.coeffs[i], coeffs[i]);
    }

    // Initialise the secrets and data structures
    generate_setup(s1, s2, &secret, secrets_len);
    TEST_CHECK(C_KZG_OK == new_fft_settings(&fs1, 4)); // ln_2 of poly_len
    TEST_CHECK(C_KZG_OK == new_kzg_settings(&ks1, &fs1, s1, s2, secrets_len));

    // Commit to the polynomial
    commit_to_poly(&commitment, &ks1, &p);

    TEST_CHECK(C_KZG_OK == new_fft_settings(&fs2, coset_scale));
    TEST_CHECK(C_KZG_OK == new_kzg_settings(&ks2, &fs2, s1, s2, secrets_len));

    // Compute proof at the points [x * root_i] 0 <= i < coset_len
    fr_from_uint64(&x, 5431);
    TEST_CHECK(C_KZG_OK == compute_proof_multi(&proof, &ks2, &p, &x, coset_len));

    // The ys are the values of the polynomial at the points above
    for (int i = 0; i < coset_len; i++) {
        blst_fr_mul(&tmp, &x, &ks2.fs->expanded_roots_of_unity[i]);
        eval_poly(&ys[i], &p, &tmp);
    }

    // Verify the proof
    TEST_CHECK(check_proof_multi(&ks2, &commitment, &proof, &x, ys, coset_len));

    free_fft_settings(&fs1);
    free_fft_settings(&fs2);
    free_poly(p);
    free(s1);
    free(s2);
}

void proof_single_error(void) {
    poly p;
    blst_p1 proof;
    KZGSettings ks;
    blst_fr x;

    // Check it barfs on a constant polynomial
    init_poly(&p, 1);

    fr_from_uint64(&x, 1234);
    TEST_CHECK(C_KZG_BADARGS == compute_proof_single(&proof, &ks, &p, &x));

    free_poly(p);
}

TEST_LIST =
    {
     {"KZG_PROOFS_TEST", title},
     {"proof_single", proof_single},
     {"proof_multi", proof_multi},
     {"proof_single_error", proof_single},
     { NULL, NULL }     /* zero record marks the end of the list */
    };