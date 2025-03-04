/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>
#include "internal/nelem.h"
#include "testutil.h"

#ifndef OPENSSL_NO_EC
# include <openssl/ec.h>
# ifndef OPENSSL_NO_ENGINE
#  include <openssl/engine.h>
# endif
# include <openssl/err.h>
# include <openssl/obj_mac.h>
# include <openssl/objects.h>
# include <openssl/rand.h>
# include <openssl/bn.h>
# include <openssl/opensslconf.h>

static size_t crv_len = 0;
static EC_builtin_curve *curves = NULL;

/* test multiplication with group order, long and negative scalars */
static int group_order_tests(EC_GROUP *group)
{
    BIGNUM *n1 = NULL, *n2 = NULL, *order = NULL;
    EC_POINT *P = NULL, *Q = NULL, *R = NULL, *S = NULL;
    const EC_POINT *G = NULL;
    BN_CTX *ctx = NULL;
    int i = 0, r = 0;

    if (!TEST_ptr(n1 = BN_new())
        || !TEST_ptr(n2 = BN_new())
        || !TEST_ptr(order = BN_new())
        || !TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(G = EC_GROUP_get0_generator(group))
        || !TEST_ptr(P = EC_POINT_new(group))
        || !TEST_ptr(Q = EC_POINT_new(group))
        || !TEST_ptr(R = EC_POINT_new(group))
        || !TEST_ptr(S = EC_POINT_new(group)))
        goto err;

    if (!TEST_true(EC_GROUP_get_order(group, order, ctx))
        || !TEST_true(EC_POINT_mul(group, Q, order, NULL, NULL, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, Q))
        || !TEST_true(EC_GROUP_precompute_mult(group, ctx))
        || !TEST_true(EC_POINT_mul(group, Q, order, NULL, NULL, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, Q))
        || !TEST_true(EC_POINT_copy(P, G))
        || !TEST_true(BN_one(n1))
        || !TEST_true(EC_POINT_mul(group, Q, n1, NULL, NULL, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, Q, P, ctx))
        || !TEST_true(BN_sub(n1, order, n1))
        || !TEST_true(EC_POINT_mul(group, Q, n1, NULL, NULL, ctx))
        || !TEST_true(EC_POINT_invert(group, Q, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, Q, P, ctx)))
        goto err;

    for (i = 1; i <= 2; i++) {
        const BIGNUM *scalars[6];
        const EC_POINT *points[6];

        if (!TEST_true(BN_set_word(n1, i))
            /*
             * If i == 1, P will be the predefined generator for which
             * EC_GROUP_precompute_mult has set up precomputation.
             */
            || !TEST_true(EC_POINT_mul(group, P, n1, NULL, NULL, ctx))
            || (i == 1 && !TEST_int_eq(0, EC_POINT_cmp(group, P, G, ctx)))
            || !TEST_true(BN_one(n1))
            /* n1 = 1 - order */
            || !TEST_true(BN_sub(n1, n1, order))
            || !TEST_true(EC_POINT_mul(group, Q, NULL, P, n1, ctx))
            || !TEST_int_eq(0, EC_POINT_cmp(group, Q, P, ctx))

            /* n2 = 1 + order */
            || !TEST_true(BN_add(n2, order, BN_value_one()))
            || !TEST_true(EC_POINT_mul(group, Q, NULL, P, n2, ctx))
            || !TEST_int_eq(0, EC_POINT_cmp(group, Q, P, ctx))

            /* n2 = (1 - order) * (1 + order) = 1 - order^2 */
            || !TEST_true(BN_mul(n2, n1, n2, ctx))
            || !TEST_true(EC_POINT_mul(group, Q, NULL, P, n2, ctx))
            || !TEST_int_eq(0, EC_POINT_cmp(group, Q, P, ctx)))
            goto err;

        /* n2 = order^2 - 1 */
        BN_set_negative(n2, 0);
        if (!TEST_true(EC_POINT_mul(group, Q, NULL, P, n2, ctx))
            /* Add P to verify the result. */
            || !TEST_true(EC_POINT_add(group, Q, Q, P, ctx))
            || !TEST_true(EC_POINT_is_at_infinity(group, Q))

            /* Exercise EC_POINTs_mul, including corner cases. */
            || !TEST_false(EC_POINT_is_at_infinity(group, P)))
            goto err;

        scalars[0] = scalars[1] = BN_value_one();
        points[0]  = points[1]  = P;

        if (!TEST_true(EC_POINTs_mul(group, R, NULL, 2, points, scalars, ctx))
            || !TEST_true(EC_POINT_dbl(group, S, points[0], ctx))
            || !TEST_int_eq(0, EC_POINT_cmp(group, R, S, ctx)))
            goto err;

        scalars[0] = n1;
        points[0] = Q;          /* => infinity */
        scalars[1] = n2;
        points[1] = P;          /* => -P */
        scalars[2] = n1;
        points[2] = Q;          /* => infinity */
        scalars[3] = n2;
        points[3] = Q;          /* => infinity */
        scalars[4] = n1;
        points[4] = P;          /* => P */
        scalars[5] = n2;
        points[5] = Q;          /* => infinity */
        if (!TEST_true(EC_POINTs_mul(group, P, NULL, 6, points, scalars, ctx))
            || !TEST_true(EC_POINT_is_at_infinity(group, P)))
            goto err;
    }

    r = 1;
err:
    if (r == 0 && i != 0)
        TEST_info(i == 1 ? "allowing precomputation" :
                           "without precomputation");
    EC_POINT_free(P);
    EC_POINT_free(Q);
    EC_POINT_free(R);
    EC_POINT_free(S);
    BN_free(n1);
    BN_free(n2);
    BN_free(order);
    BN_CTX_free(ctx);
    return r;
}

static int prime_field_tests(void)
{
    BN_CTX *ctx = NULL;
    BIGNUM *p = NULL, *a = NULL, *b = NULL, *scalar3 = NULL;
    EC_GROUP *group = NULL, *tmp = NULL;
    EC_GROUP *P_160 = NULL, *P_192 = NULL, *P_224 = NULL,
             *P_256 = NULL, *P_384 = NULL, *P_521 = NULL;
    EC_POINT *P = NULL, *Q = NULL, *R = NULL;
    BIGNUM *x = NULL, *y = NULL, *z = NULL, *yplusone = NULL;
    const EC_POINT *points[4];
    const BIGNUM *scalars[4];
    unsigned char buf[100];
    size_t len, r = 0;
    int k;

    if (!TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(p = BN_new())
        || !TEST_ptr(a = BN_new())
        || !TEST_ptr(b = BN_new())
        || !TEST_true(BN_hex2bn(&p, "17"))
        || !TEST_true(BN_hex2bn(&a, "1"))
        || !TEST_true(BN_hex2bn(&b, "1"))
        /*
         * applications should use EC_GROUP_new_curve_GFp so
         * that the library gets to choose the EC_METHOD
         */
        || !TEST_ptr(group = EC_GROUP_new(EC_GFp_mont_method()))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_ptr(tmp = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(tmp, group)))
        goto err;
    EC_GROUP_free(group);
    group = tmp;
    tmp = NULL;

    if (!TEST_true(EC_GROUP_get_curve(group, p, a, b, ctx)))
        goto err;

    TEST_info("Curve defined by Weierstrass equation");
    TEST_note("     y^2 = x^3 + a*x + b (mod p)");
    test_output_bignum("a", a);
    test_output_bignum("b", b);
    test_output_bignum("p", p);

    buf[0] = 0;
    if (!TEST_ptr(P = EC_POINT_new(group))
        || !TEST_ptr(Q = EC_POINT_new(group))
        || !TEST_ptr(R = EC_POINT_new(group))
        || !TEST_true(EC_POINT_set_to_infinity(group, P))
        || !TEST_true(EC_POINT_is_at_infinity(group, P))
        || !TEST_true(EC_POINT_oct2point(group, Q, buf, 1, ctx))
        || !TEST_true(EC_POINT_add(group, P, P, Q, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, P))
        || !TEST_ptr(x = BN_new())
        || !TEST_ptr(y = BN_new())
        || !TEST_ptr(z = BN_new())
        || !TEST_ptr(yplusone = BN_new())
        || !TEST_true(BN_hex2bn(&x, "D"))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, Q, x, 1, ctx)))
        goto err;

    if (!TEST_int_gt(EC_POINT_is_on_curve(group, Q, ctx), 0)) {
        if (!TEST_true(EC_POINT_get_affine_coordinates(group, Q, x, y, ctx)))
            goto err;
        TEST_info("Point is not on curve");
        test_output_bignum("x", x);
        test_output_bignum("y", y);
        goto err;
    }

    TEST_note("A cyclic subgroup:");
    k = 100;
    do {
        if (!TEST_int_ne(k--, 0))
            goto err;

        if (EC_POINT_is_at_infinity(group, P)) {
            TEST_note("     point at infinity");
        } else {
            if (!TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y,
                                                           ctx)))
                goto err;

            test_output_bignum("x", x);
            test_output_bignum("y", y);
        }

        if (!TEST_true(EC_POINT_copy(R, P))
            || !TEST_true(EC_POINT_add(group, P, P, Q, ctx)))
            goto err;

    } while (!EC_POINT_is_at_infinity(group, P));

    if (!TEST_true(EC_POINT_add(group, P, Q, R, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, P)))
        goto err;

    len =
        EC_POINT_point2oct(group, Q, POINT_CONVERSION_COMPRESSED, buf,
                           sizeof(buf), ctx);
    if (!TEST_size_t_ne(len, 0)
        || !TEST_true(EC_POINT_oct2point(group, P, buf, len, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, Q, ctx)))
        goto err;
    test_output_memory("Generator as octet string, compressed form:",
                       buf, len);

    len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_UNCOMPRESSED,
                             buf, sizeof(buf), ctx);
    if (!TEST_size_t_ne(len, 0)
        || !TEST_true(EC_POINT_oct2point(group, P, buf, len, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, Q, ctx)))
        goto err;
    test_output_memory("Generator as octet string, uncompressed form:",
                       buf, len);

    len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_HYBRID,
                             buf, sizeof(buf), ctx);
    if (!TEST_size_t_ne(len, 0)
        || !TEST_true(EC_POINT_oct2point(group, P, buf, len, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, Q, ctx)))
        goto err;
    test_output_memory("Generator as octet string, hybrid form:",
                       buf, len);

    if (!TEST_true(EC_POINT_get_Jprojective_coordinates_GFp(group, R, x, y, z,
                                                            ctx)))
        goto err;
    TEST_info("A representation of the inverse of that generator in");
    TEST_note("Jacobian projective coordinates");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    test_output_bignum("z", z);

    if (!TEST_true(EC_POINT_invert(group, P, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, R, ctx))

    /*
     * Curve secp160r1 (Certicom Research SEC 2 Version 1.0, section 2.4.2,
     * 2000) -- not a NIST curve, but commonly used
     */

        || !TEST_true(BN_hex2bn(&p,                         "FFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFF7FFFFFFF"))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a,                         "FFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFF7FFFFFFC"))
        || !TEST_true(BN_hex2bn(&b,                         "1C97BEFC"
                                    "54BD7A8B65ACF89F81D4D4ADC565FA45"))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_true(BN_hex2bn(&x,                         "4A96B568"
                                    "8EF573284664698968C38BB913CBFC82"))
        || !TEST_true(BN_hex2bn(&y,                         "23a62855"
                                    "3168947d59dcc912042351377ac5fb32"))
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone,
                                                       ctx))
        || !TEST_true(EC_POINT_set_affine_coordinates(group, P, x, y, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z,                       "0100000000"
                                    "000000000001F4C8F927AED3CA752257"))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, BN_value_one()))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;
    TEST_info("SEC2 curve secp160r1 -- Generator");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z,                         "23a62855"
                                 "3168947d59dcc912042351377ac5fb32"))
        || !TEST_BN_eq(y, z)
        || !TEST_int_eq(EC_GROUP_get_degree(group), 160)
        || !group_order_tests(group)
        || !TEST_ptr(P_160 = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(P_160, group))

    /* Curve P-192 (FIPS PUB 186-2, App. 6) */

        || !TEST_true(BN_hex2bn(&p,                 "FFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFF"))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a,                 "FFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFC"))
        || !TEST_true(BN_hex2bn(&b,                 "64210519E59C80E7"
                                    "0FA7E9AB72243049FEB8DEECC146B9B1"))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_true(BN_hex2bn(&x,                 "188DA80EB03090F6"
                                    "7CBF20EB43A18800F4FF0AFD82FF1012"))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, P, x, 1, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z,                 "FFFFFFFFFFFFFFFF"
                                    "FFFFFFFF99DEF836146BC9B1B4D22831"))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, BN_value_one()))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;

    TEST_info("NIST curve P-192 -- Generator");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z,                 "07192B95FFC8DA78"
                                 "631011ED6B24CDD573F977A11E794811"))
        || !TEST_BN_eq(y, z)
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone,
                                                       ctx))
        || !TEST_int_eq(EC_GROUP_get_degree(group), 192)
        || !group_order_tests(group)
        || !TEST_ptr(P_192 = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(P_192, group))

    /* Curve P-224 (FIPS PUB 186-2, App. 6) */

        || !TEST_true(BN_hex2bn(&p,         "FFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFF000000000000000000000001"))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a,         "FFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFE"))
        || !TEST_true(BN_hex2bn(&b,         "B4050A850C04B3ABF5413256"
                                    "5044B0B7D7BFD8BA270B39432355FFB4"))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_true(BN_hex2bn(&x,         "B70E0CBD6BB4BF7F321390B9"
                                    "4A03C1D356C21122343280D6115C1D21"))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, P, x, 0, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z,         "FFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFF16A2E0B8F03E13DD29455C5C2A3D"))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, BN_value_one()))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;

    TEST_info("NIST curve P-224 -- Generator");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z,         "BD376388B5F723FB4C22DFE6"
                                 "CD4375A05A07476444D5819985007E34"))
        || !TEST_BN_eq(y, z)
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone,
                                                       ctx))
        || !TEST_int_eq(EC_GROUP_get_degree(group), 224)
        || !group_order_tests(group)
        || !TEST_ptr(P_224 = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(P_224, group))

    /* Curve P-256 (FIPS PUB 186-2, App. 6) */

        || !TEST_true(BN_hex2bn(&p, "FFFFFFFF000000010000000000000000"
                                    "00000000FFFFFFFFFFFFFFFFFFFFFFFF"))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a, "FFFFFFFF000000010000000000000000"
                                    "00000000FFFFFFFFFFFFFFFFFFFFFFFC"))
        || !TEST_true(BN_hex2bn(&b, "5AC635D8AA3A93E7B3EBBD55769886BC"
                                    "651D06B0CC53B0F63BCE3C3E27D2604B"))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))

        || !TEST_true(BN_hex2bn(&x, "6B17D1F2E12C4247F8BCE6E563A440F2"
                                    "77037D812DEB33A0F4A13945D898C296"))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, P, x, 1, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z, "FFFFFFFF00000000FFFFFFFFFFFFFFFF"
                                    "BCE6FAADA7179E84F3B9CAC2FC632551"))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, BN_value_one()))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;

    TEST_info("NIST curve P-256 -- Generator");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z, "4FE342E2FE1A7F9B8EE7EB4A7C0F9E16"
                                 "2BCE33576B315ECECBB6406837BF51F5"))
        || !TEST_BN_eq(y, z)
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone,
                                                       ctx))
        || !TEST_int_eq(EC_GROUP_get_degree(group), 256)
        || !group_order_tests(group)
        || !TEST_ptr(P_256 = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(P_256, group))

    /* Curve P-384 (FIPS PUB 186-2, App. 6) */

        || !TEST_true(BN_hex2bn(&p, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE"
                                    "FFFFFFFF0000000000000000FFFFFFFF"))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE"
                                    "FFFFFFFF0000000000000000FFFFFFFC"))
        || !TEST_true(BN_hex2bn(&b, "B3312FA7E23EE7E4988E056BE3F82D19"
                                    "181D9C6EFE8141120314088F5013875A"
                                    "C656398D8A2ED19D2A85C8EDD3EC2AEF"))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))

        || !TEST_true(BN_hex2bn(&x, "AA87CA22BE8B05378EB1C71EF320AD74"
                                    "6E1D3B628BA79B9859F741E082542A38"
                                    "5502F25DBF55296C3A545E3872760AB7"))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, P, x, 1, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFC7634D81F4372DDF"
                                    "581A0DB248B0A77AECEC196ACCC52973"))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, BN_value_one()))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;

    TEST_info("NIST curve P-384 -- Generator");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z, "3617DE4A96262C6F5D9E98BF9292DC29"
                                 "F8F41DBD289A147CE9DA3113B5F0B8C0"
                                 "0A60B1CE1D7E819D7A431D7C90EA0E5F"))
        || !TEST_BN_eq(y, z)
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone,
                                                       ctx))
        || !TEST_int_eq(EC_GROUP_get_degree(group), 384)
        || !group_order_tests(group)
        || !TEST_ptr(P_384 = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(P_384, group))

    /* Curve P-521 (FIPS PUB 186-2, App. 6) */
        || !TEST_true(BN_hex2bn(&p,                              "1FF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a,                              "1FF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC"))
        || !TEST_true(BN_hex2bn(&b,                              "051"
                                    "953EB9618E1C9A1F929A21A0B68540EE"
                                    "A2DA725B99B315F3B8B489918EF109E1"
                                    "56193951EC7E937B1652C0BD3BB1BF07"
                                    "3573DF883D2C34F1EF451FD46B503F00"))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_true(BN_hex2bn(&x,                               "C6"
                                    "858E06B70404E9CD9E3ECB662395B442"
                                    "9C648139053FB521F828AF606B4D3DBA"
                                    "A14B5E77EFE75928FE1DC127A2FFA8DE"
                                    "3348B3C1856A429BF97E7E31C2E5BD66"))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, P, x, 0, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z,                              "1FF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                                    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFA"
                                    "51868783BF2F966B7FCC0148F709A5D0"
                                    "3BB5C9B8899C47AEBB6FB71E91386409"))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, BN_value_one()))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;

    TEST_info("NIST curve P-521 -- Generator");
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z,                              "118"
                                 "39296A789A3BC0045C8A5FB42C7D1BD9"
                                 "98F54449579B446817AFBD17273E662C"
                                 "97EE72995EF42640C550B9013FAD0761"
                                 "353C7086A272C24088BE94769FD16650"))
        || !TEST_BN_eq(y, z)
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone,
                                                       ctx))
        || !TEST_int_eq(EC_GROUP_get_degree(group), 521)
        || !group_order_tests(group)
        || !TEST_ptr(P_521 = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(P_521, group))

    /* more tests using the last curve */

    /* Restore the point that got mangled in the (x, y + 1) test. */
        || !TEST_true(EC_POINT_set_affine_coordinates(group, P, x, y, ctx))
        || !TEST_true(EC_POINT_copy(Q, P))
        || !TEST_false(EC_POINT_is_at_infinity(group, Q))
        || !TEST_true(EC_POINT_dbl(group, P, P, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(EC_POINT_invert(group, Q, ctx))       /* P = -2Q */
        || !TEST_true(EC_POINT_add(group, R, P, Q, ctx))
        || !TEST_true(EC_POINT_add(group, R, R, Q, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, R))    /* R = P + 2Q */
        || !TEST_false(EC_POINT_is_at_infinity(group, Q)))
        goto err;
    points[0] = Q;
    points[1] = Q;
    points[2] = Q;
    points[3] = Q;

    if (!TEST_true(EC_GROUP_get_order(group, z, ctx))
        || !TEST_true(BN_add(y, z, BN_value_one()))
        || !TEST_BN_even(y)
        || !TEST_true(BN_rshift1(y, y)))
        goto err;
    scalars[0] = y;         /* (group order + 1)/2, so y*Q + y*Q = Q */
    scalars[1] = y;

    TEST_note("combined multiplication ...");

    /* z is still the group order */
    if (!TEST_true(EC_POINTs_mul(group, P, NULL, 2, points, scalars, ctx))
        || !TEST_true(EC_POINTs_mul(group, R, z, 2, points, scalars, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, R, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, R, Q, ctx))
        || !TEST_true(BN_rand(y, BN_num_bits(y), 0, 0))
        || !TEST_true(BN_add(z, z, y)))
        goto err;
    BN_set_negative(z, 1);
    scalars[0] = y;
    scalars[1] = z;         /* z = -(order + y) */

    if (!TEST_true(EC_POINTs_mul(group, P, NULL, 2, points, scalars, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, P))
        || !TEST_true(BN_rand(x, BN_num_bits(y) - 1, 0, 0))
        || !TEST_true(BN_add(z, x, y)))
        goto err;
    BN_set_negative(z, 1);
    scalars[0] = x;
    scalars[1] = y;
    scalars[2] = z;         /* z = -(x+y) */

    if (!TEST_ptr(scalar3 = BN_new()))
        goto err;
    BN_zero(scalar3);
    scalars[3] = scalar3;

    if (!TEST_true(EC_POINTs_mul(group, P, NULL, 4, points, scalars, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, P)))
        goto err;

    TEST_note(" ok\n");


    r = 1;
err:
    BN_CTX_free(ctx);
    BN_free(p);
    BN_free(a);
    BN_free(b);
    EC_GROUP_free(group);
    EC_GROUP_free(tmp);
    EC_POINT_free(P);
    EC_POINT_free(Q);
    EC_POINT_free(R);
    BN_free(x);
    BN_free(y);
    BN_free(z);
    BN_free(yplusone);
    BN_free(scalar3);

    EC_GROUP_free(P_160);
    EC_GROUP_free(P_192);
    EC_GROUP_free(P_224);
    EC_GROUP_free(P_256);
    EC_GROUP_free(P_384);
    EC_GROUP_free(P_521);
    return r;
}

# ifndef OPENSSL_NO_EC2M

static struct c2_curve_test {
    const char *name;
    const char *p;
    const char *a;
    const char *b;
    const char *x;
    const char *y;
    int ybit;
    const char *order;
    const char *cof;
    int degree;
} char2_curve_tests[] = {
    /* Curve K-163 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve K-163",
        "0800000000000000000000000000000000000000C9",
        "1",
        "1",
        "02FE13C0537BBC11ACAA07D793DE4E6D5E5C94EEE8",
        "0289070FB05D38FF58321F2E800536D538CCDAA3D9",
        1, "04000000000000000000020108A2E0CC0D99F8A5EF", "2", 163
    },
    /* Curve B-163 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve B-163",
        "0800000000000000000000000000000000000000C9",
        "1",
        "020A601907B8C953CA1481EB10512F78744A3205FD",
        "03F0EBA16286A2D57EA0991168D4994637E8343E36",
        "00D51FBC6C71A0094FA2CDD545B11C5C0C797324F1",
        1, "040000000000000000000292FE77E70C12A4234C33", "2", 163
    },
    /* Curve K-233 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve K-233",
        "020000000000000000000000000000000000000004000000000000000001",
        "0",
        "1",
        "017232BA853A7E731AF129F22FF4149563A419C26BF50A4C9D6EEFAD6126",
        "01DB537DECE819B7F70F555A67C427A8CD9BF18AEB9B56E0C11056FAE6A3",
        0,
        "008000000000000000000000000000069D5BB915BCD46EFB1AD5F173ABDF",
        "4", 233
    },
    /* Curve B-233 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve B-233",
        "020000000000000000000000000000000000000004000000000000000001",
        "000000000000000000000000000000000000000000000000000000000001",
        "0066647EDE6C332C7F8C0923BB58213B333B20E9CE4281FE115F7D8F90AD",
        "00FAC9DFCBAC8313BB2139F1BB755FEF65BC391F8B36F8F8EB7371FD558B",
        "01006A08A41903350678E58528BEBF8A0BEFF867A7CA36716F7E01F81052",
        1,
        "01000000000000000000000000000013E974E72F8A6922031D2603CFE0D7",
        "2", 233
    },
    /* Curve K-283 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve K-283",
                                                                "08000000"
        "00000000000000000000000000000000000000000000000000000000000010A1",
        "0",
        "1",
                                                                "0503213F"
        "78CA44883F1A3B8162F188E553CD265F23C1567A16876913B0C2AC2458492836",
                                                                "01CCDA38"
        "0F1C9E318D90F95D07E5426FE87E45C0E8184698E45962364E34116177DD2259",
        0,
                                                                "01FFFFFF"
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFE9AE2ED07577265DFF7F94451E061E163C61",
        "4", 283
    },
    /* Curve B-283 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve B-283",
                                                                "08000000"
        "00000000000000000000000000000000000000000000000000000000000010A1",
                                                                "00000000"
        "0000000000000000000000000000000000000000000000000000000000000001",
                                                                "027B680A"
        "C8B8596DA5A4AF8A19A0303FCA97FD7645309FA2A581485AF6263E313B79A2F5",
                                                                "05F93925"
        "8DB7DD90E1934F8C70B0DFEC2EED25B8557EAC9C80E2E198F8CDBECD86B12053",
                                                                "03676854"
        "FE24141CB98FE6D4B20D02B4516FF702350EDDB0826779C813F0DF45BE8112F4",
        1,
                                                                "03FFFFFF"
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFEF90399660FC938A90165B042A7CEFADB307",
        "2", 283
    },
    /* Curve K-409 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve K-409",
                                "0200000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000008000000000000000000001",
        "0",
        "1",
                                "0060F05F658F49C1AD3AB1890F7184210EFD0987"
        "E307C84C27ACCFB8F9F67CC2C460189EB5AAAA62EE222EB1B35540CFE9023746",
                                "01E369050B7C4E42ACBA1DACBF04299C3460782F"
        "918EA427E6325165E9EA10E3DA5F6C42E9C55215AA9CA27A5863EC48D8E0286B",
        1,
                                "007FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
        "FFFFFFFFFFFFFE5F83B2D4EA20400EC4557D5ED3E3E7CA5B4B5C83B8E01E5FCF",
        "4", 409
    },
    /* Curve B-409 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve B-409",
                                "0200000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000008000000000000000000001",
                                "0000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000001",
                                "0021A5C2C8EE9FEB5C4B9A753B7B476B7FD6422E"
        "F1F3DD674761FA99D6AC27C8A9A197B272822F6CD57A55AA4F50AE317B13545F",
                                "015D4860D088DDB3496B0C6064756260441CDE4A"
        "F1771D4DB01FFE5B34E59703DC255A868A1180515603AEAB60794E54BB7996A7",
                                "0061B1CFAB6BE5F32BBFA78324ED106A7636B9C5"
        "A7BD198D0158AA4F5488D08F38514F1FDF4B4F40D2181B3681C364BA0273C706",
        1,
                                "0100000000000000000000000000000000000000"
        "00000000000001E2AAD6A612F33307BE5FA47C3C9E052F838164CD37D9A21173",
        "2", 409
    },
    /* Curve K-571 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve K-571",
                                                         "800000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000425",
        "0",
        "1",
                                                        "026EB7A859923FBC"
        "82189631F8103FE4AC9CA2970012D5D46024804801841CA44370958493B205E6"
        "47DA304DB4CEB08CBBD1BA39494776FB988B47174DCA88C7E2945283A01C8972",
                                                        "0349DC807F4FBF37"
        "4F4AEADE3BCA95314DD58CEC9F307A54FFC61EFC006D8A2C9D4979C0AC44AEA7"
        "4FBEBBB9F772AEDCB620B01A7BA7AF1B320430C8591984F601CD4C143EF1C7A3",
        0,
                                                        "0200000000000000"
        "00000000000000000000000000000000000000000000000000000000131850E1"
        "F19A63E4B391A8DB917F4138B630D84BE5D639381E91DEB45CFE778F637C1001",
        "4", 571
    },
    /* Curve B-571 (FIPS PUB 186-2, App. 6) */
    {
        "NIST curve B-571",
                                                         "800000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000425",
                                                        "0000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000001",
                                                        "02F40E7E2221F295"
        "DE297117B7F3D62F5C6A97FFCB8CEFF1CD6BA8CE4A9A18AD84FFABBD8EFA5933"
        "2BE7AD6756A66E294AFD185A78FF12AA520E4DE739BACA0C7FFEFF7F2955727A",
                                                        "0303001D34B85629"
        "6C16C0D40D3CD7750A93D1D2955FA80AA5F40FC8DB7B2ABDBDE53950F4C0D293"
        "CDD711A35B67FB1499AE60038614F1394ABFA3B4C850D927E1E7769C8EEC2D19",
                                                        "037BF27342DA639B"
        "6DCCFFFEB73D69D78C6C27A6009CBBCA1980F8533921E8A684423E43BAB08A57"
        "6291AF8F461BB2A8B3531D2F0485C19B16E2F1516E23DD3C1A4827AF1B8AC15B",
        1,
                                                        "03FFFFFFFFFFFFFF"
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE661CE18"
        "FF55987308059B186823851EC7DD9CA1161DE93D5174D66E8382E9BB2FE84E47",
        "2", 571
    }
};

static int char2_curve_test(int n)
{
    int r = 0;
    BN_CTX *ctx = NULL;
    BIGNUM *p = NULL, *a = NULL, *b = NULL;
    BIGNUM *x = NULL, *y = NULL, *z = NULL, *cof = NULL, *yplusone = NULL;
    EC_GROUP *group = NULL, *variable = NULL;
    EC_POINT *P = NULL, *Q = NULL, *R = NULL;
    const EC_POINT *points[3];
    const BIGNUM *scalars[3];
    struct c2_curve_test *const test = char2_curve_tests + n;

    if (!TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(p = BN_new())
        || !TEST_ptr(a = BN_new())
        || !TEST_ptr(b = BN_new())
        || !TEST_ptr(x = BN_new())
        || !TEST_ptr(y = BN_new())
        || !TEST_ptr(z = BN_new())
        || !TEST_ptr(yplusone = BN_new())
        || !TEST_true(BN_hex2bn(&p, test->p))
        || !TEST_true(BN_hex2bn(&a, test->a))
        || !TEST_true(BN_hex2bn(&b, test->b))
        || !TEST_true(group = EC_GROUP_new(EC_GF2m_simple_method()))
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_ptr(P = EC_POINT_new(group))
        || !TEST_ptr(Q = EC_POINT_new(group))
        || !TEST_ptr(R = EC_POINT_new(group))
        || !TEST_true(BN_hex2bn(&x, test->x))
        || !TEST_true(BN_hex2bn(&y, test->y))
        || !TEST_true(BN_add(yplusone, y, BN_value_one())))
        goto err;

/* Change test based on whether binary point compression is enabled or not. */
# ifdef OPENSSL_EC_BIN_PT_COMP
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
    if (!TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone, ctx))
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, P, x,
                                                          test->y_bit,
                                                          ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z, test->order))
        || !TEST_true(BN_hex2bn(&cof, test->cof))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, cof))
        || !TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y, ctx)))
        goto err;
    TEST_info("%s -- Generator", test->name);
    test_output_bignum("x", x);
    test_output_bignum("y", y);
    /* G_y value taken from the standard: */
    if (!TEST_true(BN_hex2bn(&z, test->y))
        || !TEST_BN_eq(y, z))
        goto err;
# else
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
    if (!TEST_false(EC_POINT_set_affine_coordinates(group, P, x, yplusone, ctx))
        || !TEST_true(EC_POINT_set_affine_coordinates(group, P, x, y, ctx))
        || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
        || !TEST_true(BN_hex2bn(&z, test->order))
        || !TEST_true(BN_hex2bn(&cof, test->cof))
        || !TEST_true(EC_GROUP_set_generator(group, P, z, cof)))
        goto err;
    TEST_info("%s -- Generator:", test->name);
    test_output_bignum("x", x);
    test_output_bignum("y", y);
# endif

    if (!TEST_int_eq(EC_GROUP_get_degree(group), test->degree)
        || !group_order_tests(group)
        || !TEST_ptr(variable = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(variable, group)))
        goto err;

    /* more tests using the last curve */
    if (n == OSSL_NELEM(char2_curve_tests) - 1) {
        if (!TEST_true(EC_POINT_set_affine_coordinates(group, P, x, y, ctx))
            || !TEST_true(EC_POINT_copy(Q, P))
            || !TEST_false(EC_POINT_is_at_infinity(group, Q))
            || !TEST_true(EC_POINT_dbl(group, P, P, ctx))
            || !TEST_int_gt(EC_POINT_is_on_curve(group, P, ctx), 0)
            || !TEST_true(EC_POINT_invert(group, Q, ctx))       /* P = -2Q */
            || !TEST_true(EC_POINT_add(group, R, P, Q, ctx))
            || !TEST_true(EC_POINT_add(group, R, R, Q, ctx))
            || !TEST_true(EC_POINT_is_at_infinity(group, R))   /* R = P + 2Q */
            || !TEST_false(EC_POINT_is_at_infinity(group, Q)))
            goto err;

        points[0] = Q;
        points[1] = Q;
        points[2] = Q;

        if (!TEST_true(BN_add(y, z, BN_value_one()))
            || !TEST_BN_even(y)
            || !TEST_true(BN_rshift1(y, y)))
            goto err;
        scalars[0] = y;         /* (group order + 1)/2, so y*Q + y*Q = Q */
        scalars[1] = y;

        TEST_note("combined multiplication ...");

        /* z is still the group order */
        if (!TEST_true(EC_POINTs_mul(group, P, NULL, 2, points, scalars, ctx))
            || !TEST_true(EC_POINTs_mul(group, R, z, 2, points, scalars, ctx))
            || !TEST_int_eq(0, EC_POINT_cmp(group, P, R, ctx))
            || !TEST_int_eq(0, EC_POINT_cmp(group, R, Q, ctx)))
            goto err;

        if (!TEST_true(BN_rand(y, BN_num_bits(y), 0, 0))
            || !TEST_true(BN_add(z, z, y)))
            goto err;
        BN_set_negative(z, 1);
        scalars[0] = y;
        scalars[1] = z;         /* z = -(order + y) */

        if (!TEST_true(EC_POINTs_mul(group, P, NULL, 2, points, scalars, ctx))
            || !TEST_true(EC_POINT_is_at_infinity(group, P)))
            goto err;

        if (!TEST_true(BN_rand(x, BN_num_bits(y) - 1, 0, 0))
            || !TEST_true(BN_add(z, x, y)))
            goto err;
        BN_set_negative(z, 1);
        scalars[0] = x;
        scalars[1] = y;
        scalars[2] = z;         /* z = -(x+y) */

        if (!TEST_true(EC_POINTs_mul(group, P, NULL, 3, points, scalars, ctx))
            || !TEST_true(EC_POINT_is_at_infinity(group, P)))
            goto err;;
    }

    r = 1;
err:
    BN_CTX_free(ctx);
    BN_free(p);
    BN_free(a);
    BN_free(b);
    BN_free(x);
    BN_free(y);
    BN_free(z);
    BN_free(yplusone);
    BN_free(cof);
    EC_POINT_free(P);
    EC_POINT_free(Q);
    EC_POINT_free(R);
    EC_GROUP_free(group);
    EC_GROUP_free(variable);
    return r;
}

static int char2_field_tests(void)
{
    BN_CTX *ctx = NULL;
    BIGNUM *p = NULL, *a = NULL, *b = NULL;
    EC_GROUP *group = NULL, *tmp = NULL;
    EC_POINT *P = NULL, *Q = NULL, *R = NULL;
    BIGNUM *x = NULL, *y = NULL, *z = NULL, *cof = NULL, *yplusone = NULL;
    unsigned char buf[100];
    size_t len;
    int k, r = 0;

    if (!TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(p = BN_new())
        || !TEST_ptr(a = BN_new())
        || !TEST_ptr(b = BN_new())
        || !TEST_true(BN_hex2bn(&p, "13"))
        || !TEST_true(BN_hex2bn(&a, "3"))
        || !TEST_true(BN_hex2bn(&b, "1")))
        goto err;

    group = EC_GROUP_new(EC_GF2m_simple_method()); /* applications should use
                                                    * EC_GROUP_new_curve_GF2m
                                                    * so that the library gets
                                                    * to choose the EC_METHOD */
    if (!TEST_ptr(group)
        || !TEST_true(EC_GROUP_set_curve(group, p, a, b, ctx))
        || !TEST_ptr(tmp = EC_GROUP_new(EC_GROUP_method_of(group)))
        || !TEST_true(EC_GROUP_copy(tmp, group)))
        goto err;
    EC_GROUP_free(group);
    group = tmp;
    tmp = NULL;

    if (!TEST_true(EC_GROUP_get_curve(group, p, a, b, ctx)))
        goto err;

    TEST_info("Curve defined by Weierstrass equation");
    TEST_note("     y^2 + x*y = x^3 + a*x^2 + b (mod p)");
    test_output_bignum("a", a);
    test_output_bignum("b", b);
    test_output_bignum("p", p);

     if (!TEST_ptr(P = EC_POINT_new(group))
        || !TEST_ptr(Q = EC_POINT_new(group))
        || !TEST_ptr(R = EC_POINT_new(group))
        || !TEST_true(EC_POINT_set_to_infinity(group, P))
        || !TEST_true(EC_POINT_is_at_infinity(group, P)))
        goto err;

    buf[0] = 0;
    if (!TEST_true(EC_POINT_oct2point(group, Q, buf, 1, ctx))
        || !TEST_true(EC_POINT_add(group, P, P, Q, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, P))
        || !TEST_ptr(x = BN_new())
        || !TEST_ptr(y = BN_new())
        || !TEST_ptr(z = BN_new())
        || !TEST_ptr(cof = BN_new())
        || !TEST_ptr(yplusone = BN_new())
        || !TEST_true(BN_hex2bn(&x, "6"))
/* Change test based on whether binary point compression is enabled or not. */
#  ifdef OPENSSL_EC_BIN_PT_COMP
        || !TEST_true(EC_POINT_set_compressed_coordinates(group, Q, x, 1, ctx))
#  else
        || !TEST_true(BN_hex2bn(&y, "8"))
        || !TEST_true(EC_POINT_set_affine_coordinates(group, Q, x, y, ctx))
#  endif
       )
        goto err;
    if (!TEST_int_gt(EC_POINT_is_on_curve(group, Q, ctx), 0)) {
/* Change test based on whether binary point compression is enabled or not. */
#  ifdef OPENSSL_EC_BIN_PT_COMP
        if (!TEST_true(EC_POINT_get_affine_coordinates(group, Q, x, y, ctx)))
            goto err;
#  endif
        TEST_info("Point is not on curve");
        test_output_bignum("x", x);
        test_output_bignum("y", y);
        goto err;
    }

    TEST_note("A cyclic subgroup:");
    k = 100;
    do {
        if (!TEST_int_ne(k--, 0))
            goto err;

        if (EC_POINT_is_at_infinity(group, P))
            TEST_note("     point at infinity");
        else {
            if (!TEST_true(EC_POINT_get_affine_coordinates(group, P, x, y,
                                                           ctx)))
                goto err;

            test_output_bignum("x", x);
            test_output_bignum("y", y);
        }

        if (!TEST_true(EC_POINT_copy(R, P))
            || !TEST_true(EC_POINT_add(group, P, P, Q, ctx)))
            goto err;
    }
    while (!EC_POINT_is_at_infinity(group, P));

    if (!TEST_true(EC_POINT_add(group, P, Q, R, ctx))
        || !TEST_true(EC_POINT_is_at_infinity(group, P)))
        goto err;

/* Change test based on whether binary point compression is enabled or not. */
#  ifdef OPENSSL_EC_BIN_PT_COMP
    len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_COMPRESSED,
                             buf, sizeof(buf), ctx);
    if (!TEST_size_t_ne(len, 0)
        || !TEST_true(EC_POINT_oct2point(group, P, buf, len, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, Q, ctx)))
        goto err;
    test_output_memory("Generator as octet string, compressed form:",
                       buf, len);
#  endif

    len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_UNCOMPRESSED,
                             buf, sizeof(buf), ctx);
    if (!TEST_size_t_ne(len, 0)
        || !TEST_true(EC_POINT_oct2point(group, P, buf, len, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, Q, ctx)))
        goto err;
    test_output_memory("Generator as octet string, uncompressed form:",
                       buf, len);

/* Change test based on whether binary point compression is enabled or not. */
#  ifdef OPENSSL_EC_BIN_PT_COMP
    len =
        EC_POINT_point2oct(group, Q, POINT_CONVERSION_HYBRID, buf, sizeof(buf),
                           ctx);
    if (!TEST_size_t_ne(len, 0)
        || !TEST_true(EC_POINT_oct2point(group, P, buf, len, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, Q, ctx)))
        goto err;
    test_output_memory("Generator as octet string, hybrid form:",
                       buf, len);
#  endif

    if (!TEST_true(EC_POINT_invert(group, P, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(group, P, R, ctx)))
        goto err;

    TEST_note("\n");

    r = 1;
err:
    BN_CTX_free(ctx);
    BN_free(p);
    BN_free(a);
    BN_free(b);
    EC_GROUP_free(group);
    EC_GROUP_free(tmp);
    EC_POINT_free(P);
    EC_POINT_free(Q);
    EC_POINT_free(R);
    BN_free(x);
    BN_free(y);
    BN_free(z);
    BN_free(cof);
    BN_free(yplusone);
    return r;
}
# endif

static int internal_curve_test(int n)
{
    EC_GROUP *group = NULL;
    int nid = curves[n].nid;

    if (!TEST_ptr(group = EC_GROUP_new_by_curve_name(nid))) {
        TEST_info("EC_GROUP_new_curve_name() failed with curve %s\n",
                  OBJ_nid2sn(nid));
        return 0;
    }
    if (!TEST_true(EC_GROUP_check(group, NULL))) {
        TEST_info("EC_GROUP_check() failed with curve %s\n", OBJ_nid2sn(nid));
        EC_GROUP_free(group);
        return 0;
    }
    EC_GROUP_free(group);
    return 1;
}

static int internal_curve_test_method(int n)
{
    int r, nid = curves[n].nid;
    EC_GROUP *group;

    if (!TEST_ptr(group = EC_GROUP_new_by_curve_name(nid))) {
        TEST_info("Curve %s failed\n", OBJ_nid2sn(nid));
        return 0;
    }
    r = group_order_tests(group);
    EC_GROUP_free(group);
    return r;
}

static int group_field_test(void)
{
    int r = 1;
    BIGNUM *secp521r1_field = NULL;
    BIGNUM *sect163r2_field = NULL;
    EC_GROUP *secp521r1_group = NULL;
    EC_GROUP *sect163r2_group = NULL;

    BN_hex2bn(&secp521r1_field,
                "01FFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"
                "FFFF");


    BN_hex2bn(&sect163r2_field,
                "08000000000000000000000000000000"
                "00000000C9");

    secp521r1_group = EC_GROUP_new_by_curve_name(NID_secp521r1);
    if (BN_cmp(secp521r1_field, EC_GROUP_get0_field(secp521r1_group)))
      r = 0;

    # ifndef OPENSSL_NO_EC2M
    sect163r2_group = EC_GROUP_new_by_curve_name(NID_sect163r2);
    if (BN_cmp(sect163r2_field, EC_GROUP_get0_field(sect163r2_group)))
      r = 0;
    # endif

    EC_GROUP_free(secp521r1_group);
    EC_GROUP_free(sect163r2_group);
    BN_free(secp521r1_field);
    BN_free(sect163r2_field);
    return r;
}

# ifndef OPENSSL_NO_EC_NISTP_64_GCC_128
/*
 * nistp_test_params contains magic numbers for testing our optimized
 * implementations of several NIST curves with characteristic > 3.
 */
struct nistp_test_params {
    const EC_METHOD *(*meth) (void);
    int degree;
    /*
     * Qx, Qy and D are taken from
     * http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/ECDSA_Prime.pdf
     * Otherwise, values are standard curve parameters from FIPS 180-3
     */
    const char *p, *a, *b, *Qx, *Qy, *Gx, *Gy, *order, *d;
};

static const struct nistp_test_params nistp_tests_params[] = {
    {
     /* P-224 */
     EC_GFp_nistp224_method,
     224,
     /* p */
     "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF000000000000000000000001",
     /* a */
     "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFFFFFFFFFE",
     /* b */
     "B4050A850C04B3ABF54132565044B0B7D7BFD8BA270B39432355FFB4",
     /* Qx */
     "E84FB0B8E7000CB657D7973CF6B42ED78B301674276DF744AF130B3E",
     /* Qy */
     "4376675C6FC5612C21A0FF2D2A89D2987DF7A2BC52183B5982298555",
     /* Gx */
     "B70E0CBD6BB4BF7F321390B94A03C1D356C21122343280D6115C1D21",
     /* Gy */
     "BD376388B5F723FB4C22DFE6CD4375A05A07476444D5819985007E34",
     /* order */
     "FFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D",
     /* d */
     "3F0C488E987C80BE0FEE521F8D90BE6034EC69AE11CA72AA777481E8",
     },
    {
     /* P-256 */
     EC_GFp_nistp256_method,
     256,
     /* p */
     "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",
     /* a */
     "ffffffff00000001000000000000000000000000fffffffffffffffffffffffc",
     /* b */
     "5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b",
     /* Qx */
     "b7e08afdfe94bad3f1dc8c734798ba1c62b3a0ad1e9ea2a38201cd0889bc7a19",
     /* Qy */
     "3603f747959dbf7a4bb226e41928729063adc7ae43529e61b563bbc606cc5e09",
     /* Gx */
     "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296",
     /* Gy */
     "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5",
     /* order */
     "ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",
     /* d */
     "c477f9f65c22cce20657faa5b2d1d8122336f851a508a1ed04e479c34985bf96",
     },
    {
     /* P-521 */
     EC_GFp_nistp521_method,
     521,
     /* p */
                                                                  "1ff"
     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
     /* a */
                                                                  "1ff"
     "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffc",
     /* b */
                                                                  "051"
     "953eb9618e1c9a1f929a21a0b68540eea2da725b99b315f3b8b489918ef109e1"
     "56193951ec7e937b1652c0bd3bb1bf073573df883d2c34f1ef451fd46b503f00",
     /* Qx */
                                                                 "0098"
     "e91eef9a68452822309c52fab453f5f117c1da8ed796b255e9ab8f6410cca16e"
     "59df403a6bdc6ca467a37056b1e54b3005d8ac030decfeb68df18b171885d5c4",
     /* Qy */
                                                                 "0164"
     "350c321aecfc1cca1ba4364c9b15656150b4b78d6a48d7d28e7f31985ef17be8"
     "554376b72900712c4b83ad668327231526e313f5f092999a4632fd50d946bc2e",
     /* Gx */
                                                                   "c6"
     "858e06b70404e9cd9e3ecb662395b4429c648139053fb521f828af606b4d3dba"
     "a14b5e77efe75928fe1dc127a2ffa8de3348b3c1856a429bf97e7e31c2e5bd66",
     /* Gy */
                                                                  "118"
     "39296a789a3bc0045c8a5fb42c7d1bd998f54449579b446817afbd17273e662c"
     "97ee72995ef42640c550b9013fad0761353c7086a272c24088be94769fd16650",
     /* order */
                                                                  "1ff"
     "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffa"
     "51868783bf2f966b7fcc0148f709a5d03bb5c9b8899c47aebb6fb71e91386409",
     /* d */
                                                                 "0100"
     "085f47b8e1b8b11b7eb33028c0b2888e304bfc98501955b45bba1478dc184eee"
     "df09b86a5f7c21994406072787205e69a63709fe35aa93ba333514b24f961722",
     },
};

static int nistp_single_test(int idx)
{
    const struct nistp_test_params *test = nistp_tests_params + idx;
    BN_CTX *ctx = NULL;
    BIGNUM *p = NULL, *a = NULL, *b = NULL, *x = NULL, *y = NULL;
    BIGNUM *n = NULL, *m = NULL, *order = NULL, *yplusone = NULL;
    EC_GROUP *NISTP = NULL;
    EC_POINT *G = NULL, *P = NULL, *Q = NULL, *Q_CHECK = NULL;
    int r = 0;

    TEST_note("NIST curve P-%d (optimised implementation):",
              test->degree);
    if (!TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(p = BN_new())
        || !TEST_ptr(a = BN_new())
        || !TEST_ptr(b = BN_new())
        || !TEST_ptr(x = BN_new())
        || !TEST_ptr(y = BN_new())
        || !TEST_ptr(m = BN_new())
        || !TEST_ptr(n = BN_new())
        || !TEST_ptr(order = BN_new())
        || !TEST_ptr(yplusone = BN_new())

        || !TEST_ptr(NISTP = EC_GROUP_new(test->meth()))
        || !TEST_true(BN_hex2bn(&p, test->p))
        || !TEST_int_eq(1, BN_is_prime_ex(p, BN_prime_checks, ctx, NULL))
        || !TEST_true(BN_hex2bn(&a, test->a))
        || !TEST_true(BN_hex2bn(&b, test->b))
        || !TEST_true(EC_GROUP_set_curve(NISTP, p, a, b, ctx))
        || !TEST_ptr(G = EC_POINT_new(NISTP))
        || !TEST_ptr(P = EC_POINT_new(NISTP))
        || !TEST_ptr(Q = EC_POINT_new(NISTP))
        || !TEST_ptr(Q_CHECK = EC_POINT_new(NISTP))
        || !TEST_true(BN_hex2bn(&x, test->Qx))
        || !TEST_true(BN_hex2bn(&y, test->Qy))
        || !TEST_true(BN_add(yplusone, y, BN_value_one()))
    /*
     * When (x, y) is on the curve, (x, y + 1) is, as it happens, not,
     * and therefore setting the coordinates should fail.
     */
        || !TEST_false(EC_POINT_set_affine_coordinates(NISTP, Q_CHECK, x,
                                                       yplusone, ctx))
        || !TEST_true(EC_POINT_set_affine_coordinates(NISTP, Q_CHECK, x, y,
                                                      ctx))
        || !TEST_true(BN_hex2bn(&x, test->Gx))
        || !TEST_true(BN_hex2bn(&y, test->Gy))
        || !TEST_true(EC_POINT_set_affine_coordinates(NISTP, G, x, y, ctx))
        || !TEST_true(BN_hex2bn(&order, test->order))
        || !TEST_true(EC_GROUP_set_generator(NISTP, G, order, BN_value_one()))
        || !TEST_int_eq(EC_GROUP_get_degree(NISTP), test->degree))
        goto err;

    TEST_note("NIST test vectors ... ");
    if (!TEST_true(BN_hex2bn(&n, test->d)))
        goto err;
    /* fixed point multiplication */
    EC_POINT_mul(NISTP, Q, n, NULL, NULL, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx)))
        goto err;
    /* random point multiplication */
    EC_POINT_mul(NISTP, Q, NULL, G, n, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx))

        /* set generator to P = 2*G, where G is the standard generator */
        || !TEST_true(EC_POINT_dbl(NISTP, P, G, ctx))
        || !TEST_true(EC_GROUP_set_generator(NISTP, P, order, BN_value_one()))
        /* set the scalar to m=n/2, where n is the NIST test scalar */
        || !TEST_true(BN_rshift(m, n, 1)))
        goto err;

    /* test the non-standard generator */
    /* fixed point multiplication */
    EC_POINT_mul(NISTP, Q, m, NULL, NULL, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx)))
        goto err;
    /* random point multiplication */
    EC_POINT_mul(NISTP, Q, NULL, P, m, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx))

    /*
     * We have not performed precomputation so have_precompute mult should be
     * false
     */
        || !TEST_false(EC_GROUP_have_precompute_mult(NISTP))

    /* now repeat all tests with precomputation */
        || !TEST_true(EC_GROUP_precompute_mult(NISTP, ctx))
        || !TEST_true(EC_GROUP_have_precompute_mult(NISTP)))
        goto err;

    /* fixed point multiplication */
    EC_POINT_mul(NISTP, Q, m, NULL, NULL, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx)))
        goto err;
    /* random point multiplication */
    EC_POINT_mul(NISTP, Q, NULL, P, m, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx))

    /* reset generator */
        || !TEST_true(EC_GROUP_set_generator(NISTP, G, order, BN_value_one())))
        goto err;
    /* fixed point multiplication */
    EC_POINT_mul(NISTP, Q, n, NULL, NULL, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx)))
        goto err;
    /* random point multiplication */
    EC_POINT_mul(NISTP, Q, NULL, G, n, ctx);
    if (!TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, Q_CHECK, ctx)))
        goto err;

    /* regression test for felem_neg bug */
    if (!TEST_true(BN_set_word(m, 32))
        || !TEST_true(BN_set_word(n, 31))
        || !TEST_true(EC_POINT_copy(P, G))
        || !TEST_true(EC_POINT_invert(NISTP, P, ctx))
        || !TEST_true(EC_POINT_mul(NISTP, Q, m, P, n, ctx))
        || !TEST_int_eq(0, EC_POINT_cmp(NISTP, Q, G, ctx)))
      goto err;

    r = group_order_tests(NISTP);
err:
    EC_GROUP_free(NISTP);
    EC_POINT_free(G);
    EC_POINT_free(P);
    EC_POINT_free(Q);
    EC_POINT_free(Q_CHECK);
    BN_free(n);
    BN_free(m);
    BN_free(p);
    BN_free(a);
    BN_free(b);
    BN_free(x);
    BN_free(y);
    BN_free(order);
    BN_free(yplusone);
    BN_CTX_free(ctx);
    return r;
}

/*
 * Tests a point known to cause an incorrect underflow in an old version of
 * ecp_nist521.c
 */
static int underflow_test(void)
{
    BN_CTX *ctx = NULL;
    EC_GROUP *grp = NULL;
    EC_POINT *P = NULL, *Q = NULL, *R = NULL;
    BIGNUM *x1 = NULL, *y1 = NULL, *z1 = NULL, *x2 = NULL, *y2 = NULL;
    BIGNUM *k = NULL;
    int testresult = 0;
    const char *x1str =
        "1534f0077fffffe87e9adcfe000000000000000000003e05a21d2400002e031b1f4"
        "b80000c6fafa4f3c1288798d624a247b5e2ffffffffffffffefe099241900004";
    const char *p521m1 =
        "1ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
        "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe";

    ctx = BN_CTX_new();
    if (!TEST_ptr(ctx))
        return 0;

    BN_CTX_start(ctx);
    x1 = BN_CTX_get(ctx);
    y1 = BN_CTX_get(ctx);
    z1 = BN_CTX_get(ctx);
    x2 = BN_CTX_get(ctx);
    y2 = BN_CTX_get(ctx);
    k = BN_CTX_get(ctx);
    if (!TEST_ptr(k))
        goto err;

    grp = EC_GROUP_new_by_curve_name(NID_secp521r1);
    P = EC_POINT_new(grp);
    Q = EC_POINT_new(grp);
    R = EC_POINT_new(grp);
    if (!TEST_ptr(grp) || !TEST_ptr(P) || !TEST_ptr(Q) || !TEST_ptr(R))
        goto err;

    if (!TEST_int_gt(BN_hex2bn(&x1, x1str), 0)
            || !TEST_int_gt(BN_hex2bn(&y1, p521m1), 0)
            || !TEST_int_gt(BN_hex2bn(&z1, p521m1), 0)
            || !TEST_int_gt(BN_hex2bn(&k, "02"), 0)
            || !TEST_true(EC_POINT_set_Jprojective_coordinates_GFp(grp, P, x1,
                                                                   y1, z1, ctx))
            || !TEST_true(EC_POINT_mul(grp, Q, NULL, P, k, ctx))
            || !TEST_true(EC_POINT_get_affine_coordinates(grp, Q, x1, y1, ctx))
            || !TEST_true(EC_POINT_dbl(grp, R, P, ctx))
            || !TEST_true(EC_POINT_get_affine_coordinates(grp, R, x2, y2, ctx)))
        goto err;

    if (!TEST_int_eq(BN_cmp(x1, x2), 0)
            || !TEST_int_eq(BN_cmp(y1, y2), 0))
        goto err;

    testresult = 1;

 err:
    BN_CTX_end(ctx);
    EC_POINT_free(P);
    EC_POINT_free(Q);
    EC_POINT_free(R);
    EC_GROUP_free(grp);
    BN_CTX_free(ctx);

    return testresult;
}
# endif

static const unsigned char p521_named[] = {
    0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x23,
};

static const unsigned char p521_explicit[] = {
    0x30, 0x82, 0x01, 0xc3, 0x02, 0x01, 0x01, 0x30, 0x4d, 0x06, 0x07, 0x2a,
    0x86, 0x48, 0xce, 0x3d, 0x01, 0x01, 0x02, 0x42, 0x01, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x30, 0x81, 0x9f, 0x04, 0x42, 0x01, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfc, 0x04, 0x42, 0x00, 0x51, 0x95, 0x3e, 0xb9, 0x61, 0x8e, 0x1c, 0x9a,
    0x1f, 0x92, 0x9a, 0x21, 0xa0, 0xb6, 0x85, 0x40, 0xee, 0xa2, 0xda, 0x72,
    0x5b, 0x99, 0xb3, 0x15, 0xf3, 0xb8, 0xb4, 0x89, 0x91, 0x8e, 0xf1, 0x09,
    0xe1, 0x56, 0x19, 0x39, 0x51, 0xec, 0x7e, 0x93, 0x7b, 0x16, 0x52, 0xc0,
    0xbd, 0x3b, 0xb1, 0xbf, 0x07, 0x35, 0x73, 0xdf, 0x88, 0x3d, 0x2c, 0x34,
    0xf1, 0xef, 0x45, 0x1f, 0xd4, 0x6b, 0x50, 0x3f, 0x00, 0x03, 0x15, 0x00,
    0xd0, 0x9e, 0x88, 0x00, 0x29, 0x1c, 0xb8, 0x53, 0x96, 0xcc, 0x67, 0x17,
    0x39, 0x32, 0x84, 0xaa, 0xa0, 0xda, 0x64, 0xba, 0x04, 0x81, 0x85, 0x04,
    0x00, 0xc6, 0x85, 0x8e, 0x06, 0xb7, 0x04, 0x04, 0xe9, 0xcd, 0x9e, 0x3e,
    0xcb, 0x66, 0x23, 0x95, 0xb4, 0x42, 0x9c, 0x64, 0x81, 0x39, 0x05, 0x3f,
    0xb5, 0x21, 0xf8, 0x28, 0xaf, 0x60, 0x6b, 0x4d, 0x3d, 0xba, 0xa1, 0x4b,
    0x5e, 0x77, 0xef, 0xe7, 0x59, 0x28, 0xfe, 0x1d, 0xc1, 0x27, 0xa2, 0xff,
    0xa8, 0xde, 0x33, 0x48, 0xb3, 0xc1, 0x85, 0x6a, 0x42, 0x9b, 0xf9, 0x7e,
    0x7e, 0x31, 0xc2, 0xe5, 0xbd, 0x66, 0x01, 0x18, 0x39, 0x29, 0x6a, 0x78,
    0x9a, 0x3b, 0xc0, 0x04, 0x5c, 0x8a, 0x5f, 0xb4, 0x2c, 0x7d, 0x1b, 0xd9,
    0x98, 0xf5, 0x44, 0x49, 0x57, 0x9b, 0x44, 0x68, 0x17, 0xaf, 0xbd, 0x17,
    0x27, 0x3e, 0x66, 0x2c, 0x97, 0xee, 0x72, 0x99, 0x5e, 0xf4, 0x26, 0x40,
    0xc5, 0x50, 0xb9, 0x01, 0x3f, 0xad, 0x07, 0x61, 0x35, 0x3c, 0x70, 0x86,
    0xa2, 0x72, 0xc2, 0x40, 0x88, 0xbe, 0x94, 0x76, 0x9f, 0xd1, 0x66, 0x50,
    0x02, 0x42, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfa,
    0x51, 0x86, 0x87, 0x83, 0xbf, 0x2f, 0x96, 0x6b, 0x7f, 0xcc, 0x01, 0x48,
    0xf7, 0x09, 0xa5, 0xd0, 0x3b, 0xb5, 0xc9, 0xb8, 0x89, 0x9c, 0x47, 0xae,
    0xbb, 0x6f, 0xb7, 0x1e, 0x91, 0x38, 0x64, 0x09, 0x02, 0x01, 0x01,
};

/*
 * This test validates a named curve's group parameters using
 * EC_GROUP_check_named_curve(). It also checks that modifying any of the
 * group parameters results in the curve not being valid.
 */
static int check_named_curve_test(int id)
{
    int ret = 0, nid, field_nid, has_seed;
    EC_GROUP *group = NULL, *gtest = NULL;
    const EC_POINT *group_gen = NULL;
    EC_POINT *other_gen = NULL;
    BIGNUM *group_p = NULL, *group_a = NULL, *group_b = NULL;
    BIGNUM *other_p = NULL, *other_a = NULL, *other_b = NULL;
    BIGNUM *group_cofactor = NULL, *other_cofactor = NULL;
    BIGNUM *other_order = NULL;
    const BIGNUM *group_order = NULL;
    BN_CTX *bn_ctx = NULL;
    static const unsigned char invalid_seed[] = "THIS IS NOT A VALID SEED";
    static size_t invalid_seed_len = sizeof(invalid_seed);

    /* Do some setup */
    nid = curves[id].nid;
    if (!TEST_ptr(bn_ctx = BN_CTX_new())
        || !TEST_ptr(group = EC_GROUP_new_by_curve_name(nid))
        || !TEST_ptr(gtest = EC_GROUP_dup(group))
        || !TEST_ptr(group_p = BN_new())
        || !TEST_ptr(group_a = BN_new())
        || !TEST_ptr(group_b = BN_new())
        || !TEST_ptr(group_cofactor = BN_new())
        || !TEST_ptr(group_gen = EC_GROUP_get0_generator(group))
        || !TEST_ptr(group_order = EC_GROUP_get0_order(group))
        || !TEST_true(EC_GROUP_get_cofactor(group, group_cofactor, NULL))
        || !TEST_true(EC_GROUP_get_curve(group, group_p, group_a, group_b, NULL))
        || !TEST_ptr(other_gen = EC_POINT_dup(group_gen, group))
        || !TEST_true(EC_POINT_add(group, other_gen, group_gen, group_gen, NULL))
        || !TEST_ptr(other_order = BN_dup(group_order))
        || !TEST_true(BN_add_word(other_order, 1))
        || !TEST_ptr(other_a = BN_dup(group_a))
        || !TEST_true(BN_add_word(other_a, 1))
        || !TEST_ptr(other_b = BN_dup(group_b))
        || !TEST_true(BN_add_word(other_b, 1))
        || !TEST_ptr(other_cofactor = BN_dup(group_cofactor))
        || !TEST_true(BN_add_word(other_cofactor, 1)))
        goto err;

    /* Determine if the built-in curve has a seed field set */
    has_seed = (EC_GROUP_get_seed_len(group) > 0);
    field_nid = EC_METHOD_get_field_type(EC_GROUP_method_of(group));
    if (field_nid == NID_X9_62_characteristic_two_field) {
        if (!TEST_ptr(other_p = BN_dup(group_p))
            || !TEST_true(BN_lshift1(other_p, other_p)))
            goto err;
    } else {
        if (!TEST_ptr(other_p = BN_dup(group_p)))
            goto err;
        /*
         * Just choosing any arbitrary prime does not work..
         * Setting p via ec_GFp_nist_group_set_curve() needs the prime to be a
         * nist prime. So only select one of these as an alternate prime.
         */
        if (!TEST_ptr(BN_copy(other_p,
                              BN_ucmp(BN_get0_nist_prime_192(), other_p) == 0 ?
                                      BN_get0_nist_prime_256() :
                                      BN_get0_nist_prime_192())))
            goto err;
    }

    /* Passes because this is a valid curve */
    if (!TEST_int_eq(EC_GROUP_check_named_curve(group, 0, NULL), nid)
        /* Only NIST curves pass */
        || !TEST_int_eq(EC_GROUP_check_named_curve(group, 1, NULL),
                        EC_curve_nid2nist(nid) != NULL ? nid : NID_undef))
        goto err;

    /* Fail if the curve name doesn't match the parameters */
    EC_GROUP_set_curve_name(group, nid + 1);
    ERR_set_mark();
    if (!TEST_int_le(EC_GROUP_check_named_curve(group, 0, NULL), 0))
        goto err;
    ERR_pop_to_mark();

    /* Restore curve name and ensure it's passing */
    EC_GROUP_set_curve_name(group, nid);
    if (!TEST_int_eq(EC_GROUP_check_named_curve(group, 0, NULL), nid))
        goto err;

    if (!TEST_int_eq(EC_GROUP_set_seed(group, invalid_seed, invalid_seed_len),
                     invalid_seed_len))
        goto err;

    if (has_seed) {
        /*
         * If the built-in curve has a seed and we set the seed to another value
         * then it will fail the check.
         */
        if (!TEST_int_eq(EC_GROUP_check_named_curve(group, 0, NULL), 0))
            goto err;
    } else {
        /*
         * If the built-in curve does not have a seed then setting the seed will
         * pass the check (as the seed is optional).
         */
        if (!TEST_int_eq(EC_GROUP_check_named_curve(group, 0, NULL), nid))
            goto err;
    }
    /* Pass if the seed is unknown (as it is optional) */
    if (!TEST_int_eq(EC_GROUP_set_seed(group, NULL, 0), 1)
        || !TEST_int_eq(EC_GROUP_check_named_curve(group, 0, NULL), nid))
        goto err;

    /* Check that a duped group passes */
    if (!TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), nid))
        goto err;

    /* check that changing any generator parameter fails */
    if (!TEST_true(EC_GROUP_set_generator(gtest, other_gen, group_order,
                                          group_cofactor))
        || !TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), 0)
        || !TEST_true(EC_GROUP_set_generator(gtest, group_gen, other_order,
                                             group_cofactor))
        || !TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), 0)
        /* The order is not an optional field, so this should fail */
        || TEST_true(EC_GROUP_set_generator(gtest, group_gen, NULL,
                                            group_cofactor))
        || !TEST_true(EC_GROUP_set_generator(gtest, group_gen, group_order,
                                             other_cofactor))
        || !TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), 0)
        /* Check that if the cofactor is not set then it still passes */
        || !TEST_true(EC_GROUP_set_generator(gtest, group_gen, group_order,
                                             NULL))
        || !TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), nid)
        /* check that restoring the generator passes */
        || !TEST_true(EC_GROUP_set_generator(gtest, group_gen, group_order,
                                             group_cofactor))
        || !TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), nid))
        goto err;

    /*
     * check that changing any curve parameter fails
     *
     * Setting arbitrary p, a or b might fail for some EC_GROUPs
     * depending on the internal EC_METHOD implementation, hence run
     * these tests conditionally to the success of EC_GROUP_set_curve().
     */
    ERR_set_mark();
    if (EC_GROUP_set_curve(gtest, other_p, group_a, group_b, NULL)) {
        if (!TEST_int_le(EC_GROUP_check_named_curve(gtest, 0, NULL), 0))
            goto err;
    } else {
        /* clear the error stack if EC_GROUP_set_curve() failed */
        ERR_pop_to_mark();
        ERR_set_mark();
    }
    if (EC_GROUP_set_curve(gtest, group_p, other_a, group_b, NULL)) {
        if (!TEST_int_le(EC_GROUP_check_named_curve(gtest, 0, NULL), 0))
            goto err;
    } else {
        /* clear the error stack if EC_GROUP_set_curve() failed */
        ERR_pop_to_mark();
        ERR_set_mark();
    }
    if (EC_GROUP_set_curve(gtest, group_p, group_a, other_b, NULL)) {
        if (!TEST_int_le(EC_GROUP_check_named_curve(gtest, 0, NULL), 0))
            goto err;
    } else {
        /* clear the error stack if EC_GROUP_set_curve() failed */
        ERR_pop_to_mark();
        ERR_set_mark();
    }
    ERR_pop_to_mark();

    /* Check that restoring the curve parameters passes */
    if (!TEST_true(EC_GROUP_set_curve(gtest, group_p, group_a, group_b, NULL))
        || !TEST_int_eq(EC_GROUP_check_named_curve(gtest, 0, NULL), nid))
        goto err;

    ret = 1;
err:
    BN_free(group_p);
    BN_free(other_p);
    BN_free(group_a);
    BN_free(other_a);
    BN_free(group_b);
    BN_free(other_b);
    BN_free(group_cofactor);
    BN_free(other_cofactor);
    BN_free(other_order);
    EC_POINT_free(other_gen);
    EC_GROUP_free(gtest);
    EC_GROUP_free(group);
    BN_CTX_free(bn_ctx);
    return ret;
}

/*
 * This checks the lookup capability of EC_GROUP_check_named_curve()
 * when the given group was created with explicit parameters.
 *
 * It is possible to retrieve an alternative alias that does not match
 * the original nid in this case.
 */
static int check_named_curve_lookup_test(int id)
{
    int ret = 0, nid, rv = 0;
    EC_GROUP *g = NULL , *ga = NULL;
    ECPARAMETERS *p = NULL, *pa = NULL;
    BN_CTX *ctx = NULL;

    /* Do some setup */
    nid = curves[id].nid;
    if (!TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(g = EC_GROUP_new_by_curve_name(nid))
        || !TEST_ptr(p = EC_GROUP_get_ecparameters(g, NULL)))
        goto err;

    /* replace with group from explicit parameters */
    EC_GROUP_free(g);
    if (!TEST_ptr(g = EC_GROUP_new_from_ecparameters(p)))
        goto err;

    if (!TEST_int_gt(rv = EC_GROUP_check_named_curve(g, 0, NULL), 0))
        goto err;
    if (rv != nid) {
        /*
         * Found an alias:
         * fail if the returned nid is not an alias of the original group.
         *
         * The comparison here is done by comparing two explicit
         * parameter EC_GROUPs with EC_GROUP_cmp(), to ensure the
         * comparison happens with unnamed EC_GROUPs using the same
         * EC_METHODs.
         */
        if (!TEST_ptr(ga = EC_GROUP_new_by_curve_name(rv))
                || !TEST_ptr(pa = EC_GROUP_get_ecparameters(ga, NULL)))
            goto err;

        /* replace with group from explicit parameters, then compare */
        EC_GROUP_free(ga);
        if (!TEST_ptr(ga = EC_GROUP_new_from_ecparameters(pa))
                || !TEST_int_eq(EC_GROUP_cmp(g, ga, ctx), 0))
            goto err;
    }

    ret = 1;

 err:
    EC_GROUP_free(g);
    EC_GROUP_free(ga);
    ECPARAMETERS_free(p);
    ECPARAMETERS_free(pa);
    BN_CTX_free(ctx);

    return ret;
}

static int parameter_test(void)
{
    EC_GROUP *group = NULL, *group2 = NULL;
    ECPARAMETERS *ecparameters = NULL;
    unsigned char *buf = NULL;
    int r = 0, len;

    if (!TEST_ptr(group = EC_GROUP_new_by_curve_name(NID_secp384r1))
        || !TEST_ptr(ecparameters = EC_GROUP_get_ecparameters(group, NULL))
        || !TEST_ptr(group2 = EC_GROUP_new_from_ecparameters(ecparameters))
        || !TEST_int_eq(EC_GROUP_cmp(group, group2, NULL), 0))
        goto err;

    EC_GROUP_free(group);
    group = NULL;

    /* Test the named curve encoding, which should be default. */
    if (!TEST_ptr(group = EC_GROUP_new_by_curve_name(NID_secp521r1))
        || !TEST_true((len = i2d_ECPKParameters(group, &buf)) >= 0)
        || !TEST_mem_eq(buf, len, p521_named, sizeof(p521_named)))
        goto err;

    OPENSSL_free(buf);
    buf = NULL;

    /*
     * Test the explicit encoding. P-521 requires correctly zero-padding the
     * curve coefficients.
     */
    EC_GROUP_set_asn1_flag(group, OPENSSL_EC_EXPLICIT_CURVE);
    if (!TEST_true((len = i2d_ECPKParameters(group, &buf)) >= 0)
        || !TEST_mem_eq(buf, len, p521_explicit, sizeof(p521_explicit)))
        goto err;

    r = 1;
err:
    EC_GROUP_free(group);
    EC_GROUP_free(group2);
    ECPARAMETERS_free(ecparameters);
    OPENSSL_free(buf);
    return r;
}

/*-
 * For named curves, test that:
 * - the lib correctly computes the cofactor if passed a NULL or zero cofactor
 * - a nonsensical cofactor throws an error (negative test)
 * - nonsensical orders throw errors (negative tests)
 */
static int cardinality_test(int n)
{
    int ret = 0;
    int nid = curves[n].nid;
    BN_CTX *ctx = NULL;
    EC_GROUP *g1 = NULL, *g2 = NULL;
    EC_POINT *g2_gen = NULL;
    BIGNUM *g1_p = NULL, *g1_a = NULL, *g1_b = NULL, *g1_x = NULL, *g1_y = NULL,
           *g1_order = NULL, *g1_cf = NULL, *g2_cf = NULL;

   TEST_info("Curve %s cardinality test", OBJ_nid2sn(nid));

    if (!TEST_ptr(ctx = BN_CTX_new())
        || !TEST_ptr(g1 = EC_GROUP_new_by_curve_name(nid))
        || !TEST_ptr(g2 = EC_GROUP_new(EC_GROUP_method_of(g1)))) {
        EC_GROUP_free(g1);
        EC_GROUP_free(g2);
        BN_CTX_free(ctx);
        return 0;
    }

    BN_CTX_start(ctx);
    g1_p = BN_CTX_get(ctx);
    g1_a = BN_CTX_get(ctx);
    g1_b = BN_CTX_get(ctx);
    g1_x = BN_CTX_get(ctx);
    g1_y = BN_CTX_get(ctx);
    g1_order = BN_CTX_get(ctx);
    g1_cf = BN_CTX_get(ctx);

    if (!TEST_ptr(g2_cf = BN_CTX_get(ctx))
        /* pull out the explicit curve parameters */
        || !TEST_true(EC_GROUP_get_curve(g1, g1_p, g1_a, g1_b, ctx))
        || !TEST_true(EC_POINT_get_affine_coordinates(g1,
                      EC_GROUP_get0_generator(g1), g1_x, g1_y, ctx))
        || !TEST_true(BN_copy(g1_order, EC_GROUP_get0_order(g1)))
        || !TEST_true(EC_GROUP_get_cofactor(g1, g1_cf, ctx))
        /* construct g2 manually with g1 parameters */
        || !TEST_true(EC_GROUP_set_curve(g2, g1_p, g1_a, g1_b, ctx))
        || !TEST_ptr(g2_gen = EC_POINT_new(g2))
        || !TEST_true(EC_POINT_set_affine_coordinates(g2, g2_gen, g1_x, g1_y, ctx))
        /* pass NULL cofactor: lib should compute it */
        || !TEST_true(EC_GROUP_set_generator(g2, g2_gen, g1_order, NULL))
        || !TEST_true(EC_GROUP_get_cofactor(g2, g2_cf, ctx))
        || !TEST_BN_eq(g1_cf, g2_cf)
        /* pass zero cofactor: lib should compute it */
        || !TEST_true(BN_set_word(g2_cf, 0))
        || !TEST_true(EC_GROUP_set_generator(g2, g2_gen, g1_order, g2_cf))
        || !TEST_true(EC_GROUP_get_cofactor(g2, g2_cf, ctx))
        || !TEST_BN_eq(g1_cf, g2_cf)
        /* negative test for invalid cofactor */
        || !TEST_true(BN_set_word(g2_cf, 0))
        || !TEST_true(BN_sub(g2_cf, g2_cf, BN_value_one()))
        || TEST_true(EC_GROUP_set_generator(g2, g2_gen, g1_order, g2_cf))
        /* negative test for NULL order */
        || TEST_true(EC_GROUP_set_generator(g2, g2_gen, NULL, NULL))
        /* negative test for zero order */
        || !TEST_true(BN_set_word(g1_order, 0))
        || TEST_true(EC_GROUP_set_generator(g2, g2_gen, g1_order, NULL))
        /* negative test for negative order */
        || !TEST_true(BN_set_word(g2_cf, 0))
        || !TEST_true(BN_sub(g2_cf, g2_cf, BN_value_one()))
        || TEST_true(EC_GROUP_set_generator(g2, g2_gen, g1_order, NULL))
        /* negative test for too large order */
        || !TEST_true(BN_lshift(g1_order, g1_p, 2))
        || TEST_true(EC_GROUP_set_generator(g2, g2_gen, g1_order, NULL)))
        goto err;
    ret = 1;
 err:
    EC_POINT_free(g2_gen);
    EC_GROUP_free(g1);
    EC_GROUP_free(g2);
    BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    return ret;
}

static int check_ec_key_field_public_range_test(int id)
{
    int ret = 0, type = 0;
    const EC_POINT *pub = NULL;
    const EC_GROUP *group = NULL;
    const EC_METHOD *meth = NULL;
    const BIGNUM *field = NULL;
    BIGNUM *x = NULL, *y = NULL;
    EC_KEY *key = NULL;

    if (!(TEST_ptr(x = BN_new())
          && TEST_ptr(y = BN_new())
          && TEST_ptr(key = EC_KEY_new_by_curve_name(curves[id].nid))
          && TEST_ptr(group = EC_KEY_get0_group(key))
          && TEST_ptr(meth = EC_GROUP_method_of(group))
          && TEST_ptr(field = EC_GROUP_get0_field(group))
          && TEST_int_gt(EC_KEY_generate_key(key), 0)
          && TEST_int_gt(EC_KEY_check_key(key), 0)
          && TEST_ptr(pub = EC_KEY_get0_public_key(key))
          && TEST_int_gt(EC_POINT_get_affine_coordinates(group, pub, x, y,
                                                         NULL), 0)))
        goto err;

    /*
     * Make the public point out of range by adding the field (which will still
     * be the same point on the curve). The add is different for char2 fields.
     */
    type = EC_METHOD_get_field_type(meth);
#ifndef OPENSSL_NO_EC2M
    if (type == NID_X9_62_characteristic_two_field) {
        /* test for binary curves */
        if (!TEST_true(BN_GF2m_add(x, x, field)))
            goto err;
    } else
#endif
    if (type == NID_X9_62_prime_field) {
        /* test for prime curves */
        if (!TEST_true(BN_add(x, x, field)))
            goto err;
    } else {
        /* this should never happen */
        TEST_error("Unsupported EC_METHOD field_type");
        goto err;
    }
    if (!TEST_int_le(EC_KEY_set_public_key_affine_coordinates(key, x, y), 0))
        goto err;

    ret = 1;
err:
    BN_free(x);
    BN_free(y);
    EC_KEY_free(key);
    return ret;
}
#endif /* OPENSSL_NO_EC */

int setup_tests(void)
{
#ifndef OPENSSL_NO_EC
    crv_len = EC_get_builtin_curves(NULL, 0);
    if (!TEST_ptr(curves = OPENSSL_malloc(sizeof(*curves) * crv_len))
        || !TEST_true(EC_get_builtin_curves(curves, crv_len)))
        return 0;

    ADD_TEST(parameter_test);
    ADD_ALL_TESTS(cardinality_test, crv_len);
    ADD_TEST(prime_field_tests);
# ifndef OPENSSL_NO_EC2M
    ADD_TEST(char2_field_tests);
    ADD_ALL_TESTS(char2_curve_test, OSSL_NELEM(char2_curve_tests));
# endif
# ifndef OPENSSL_NO_EC_NISTP_64_GCC_128
    ADD_ALL_TESTS(nistp_single_test, OSSL_NELEM(nistp_tests_params));
    ADD_TEST(underflow_test);
# endif
    ADD_ALL_TESTS(internal_curve_test, crv_len);
    ADD_ALL_TESTS(internal_curve_test_method, crv_len);
    ADD_TEST(group_field_test);
    ADD_ALL_TESTS(check_named_curve_test, crv_len);
    ADD_ALL_TESTS(check_named_curve_lookup_test, crv_len);
    ADD_ALL_TESTS(check_ec_key_field_public_range_test, crv_len);
#endif /* OPENSSL_NO_EC */
    return 1;
}

void cleanup_tests(void)
{
#ifndef OPENSSL_NO_EC
    OPENSSL_free(curves);
#endif
}
