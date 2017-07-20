/*
 * Copyright (c) 2017 Lev Walkin <vlm@lionet.info>.
 * All rights reserved.
 * Redistribution and modifications are permitted subject to BSD license.
 */
#ifndef ASN_DISABLE_OER_SUPPORT

#include <asn_internal.h>
#include <INTEGER.h>
#include <errno.h>

asn_dec_rval_t
INTEGER_decode_oer(asn_codec_ctx_t *opt_codec_ctx, asn_TYPE_descriptor_t *td,
                   asn_oer_constraints_t *constraints, void **sptr,
                   const void *ptr, size_t size) {
    asn_INTEGER_specifics_t *specs = (asn_INTEGER_specifics_t *)td->specifics;
    asn_dec_rval_t rval = {RC_OK, 0};
    INTEGER_t *st = (INTEGER_t *)*sptr;
    asn_oer_constraint_t *ct;
    size_t req_bytes = 0; /* 0 = length determinant is required */

    (void)opt_codec_ctx;
    (void)specs;

    if(!st) {
        st = (INTEGER_t *)(*sptr = CALLOC(1, sizeof(*st)));
        if(!st) ASN__DECODE_FAILED;
    }

    FREEMEM(st->buf);
    st->buf = 0;
    st->size = 0;

    if(!constraints) constraints = td->oer_constraints;
    ct = constraints ? &constraints->value : 0;

    if(ct && (ct->flags & AOC_HAS_LOWER_BOUND) && ct->lower_bound >= 0) {
        /* X.969 08/2015 10.2(a) */
        unsigned msb;   /* Most significant bit */
        size_t useful_size;

        intmax_t ub = ct->upper_bound;
        if(ct->flags & AOC_HAS_UPPER_BOUND) {
            if(ub <= 255) {
                req_bytes = 1;
            } else if(ub <= 65535) {
                req_bytes = 2;
            } else if(ub <= 4294967295UL) {
                req_bytes = 4;
            } else if(ub <= 18446744073709551615ULL) {
                req_bytes = 8;
            }
        }

        if(req_bytes == 0) {    /* #8.6, using length determinant */
            ssize_t consumed = oer_fetch_length(ptr, size, &req_bytes);
            if(consumed == 0) {
                ASN__DECODE_STARVED;
            } else if(consumed == -1) {
                ASN__DECODE_FAILED;
            }
            rval.consumed += consumed;
            ptr = (const char *)ptr + consumed;
            size -= consumed;
        }

        if(req_bytes > size) {
            ASN__DECODE_STARVED;
        }

        /* Check most significant bit */
        msb = *(const uint8_t *)ptr >> 7; /* yields 0 or 1 */
        useful_size = msb + req_bytes;
        st->buf = (uint8_t *)MALLOC(useful_size + 1);
        if(!st->buf) {
            ASN__DECODE_FAILED;
        }

        /*
         * Record a large unsigned in a way not to confuse it
         * with signed value.
         */
        st->buf[0] = '\0';
        memcpy(st->buf + msb, ptr, req_bytes);
        st->buf[useful_size] = '\0';    /* Just in case, 0-terminate */
        st->size = useful_size;

        rval.consumed += req_bytes;
        return rval;
    } else if(ct
              && ((ct->flags
                  & (AOC_HAS_LOWER_BOUND | AOC_HAS_UPPER_BOUND))
                        == (AOC_HAS_LOWER_BOUND | AOC_HAS_UPPER_BOUND))) {
        /* X.969 08/2015 10.2(b) - no lower bound or negative lower bound */

        intmax_t lb = ct->lower_bound;
        intmax_t ub = ct->upper_bound;

        if(lb >= -128 && ub <= 127) {
            req_bytes = 1;
        } else if(lb >= -32768 && ub <= 32767) {
            req_bytes = 2;
        } else if(lb >= -2147483648L && ub <= 2147483647L) {
            req_bytes = 4;
        } else if(lb >= -9223372036854775808LL && ub <= 9223372036854775807LL) {
            req_bytes = 8;
        }
    }

    /* No lower bound and no upper bound, effectively */

    if(req_bytes == 0) {    /* #8.6, using length determinant */
        ssize_t consumed = oer_fetch_length(ptr, size, &req_bytes);
        if(consumed == 0) {
            ASN__DECODE_STARVED;
        } else if(consumed == -1) {
            ASN__DECODE_FAILED;
        }
        rval.consumed += consumed;
        ptr = (const char *)ptr + consumed;
        size -= consumed;
    }

    if(req_bytes > size) {
        ASN__DECODE_STARVED;
    }

    st->buf = (uint8_t *)MALLOC(req_bytes + 1);
    if(!st->buf) {
        ASN__DECODE_FAILED;
    }

    memcpy(st->buf, ptr, req_bytes);
    st->buf[req_bytes] = '\0'; /* Just in case, 0-terminate */
    st->size = req_bytes;

    rval.consumed += req_bytes;
    return rval;
}

/*
 * Encode as Canonical OER.
 */
asn_enc_rval_t
INTEGER_encode_oer(asn_TYPE_descriptor_t *td,
                   asn_oer_constraints_t *constraints, void *sptr,
                   asn_app_consume_bytes_f *cb, void *app_key) {
    const INTEGER_t *st = sptr;
    asn_enc_rval_t er;
    asn_oer_constraint_t *ct;
    const uint8_t *buf;
    const uint8_t *end;
    size_t useful_bytes;
    size_t req_bytes = 0;
    int encode_as_unsigned;
    int sign = 0;

    if(!st || st->size == 0) ASN__ENCODE_FAILED;

    if(!constraints) constraints = td->oer_constraints;
    ct = constraints ? &constraints->value : 0;

    er.encoded = 0;

    buf = st->buf;
    end = buf + st->size;

    encode_as_unsigned =
        ct && (ct->flags & AOC_HAS_LOWER_BOUND) && ct->lower_bound >= 0;

    sign = (buf && buf < end) ? buf[0] & 0x80 : 0;

    /* Ignore 9 leading zeroes or ones */
    if(encode_as_unsigned) {
        if(sign) {
            /* The value given is a signed value. Can't proceed. */
            ASN__ENCODE_FAILED;
        }
        /* Remove leading zeros. */
        for(; buf + 1 < end; buf++) {
            if(buf[0] != 0x0) break;
        }
    } else {
        for(; buf + 1 < end; buf++) {
            if(buf[0] == 0x0 && (buf[1] & 0x80) == 0) {
                continue;
            } else if(buf[0] == 0xff && (buf[1] & 0x80) != 0) {
                continue;
            }
            break;
        }
    }

    useful_bytes = end - buf;
    if(encode_as_unsigned) {
        intmax_t ub = ct->upper_bound;

        if(ub <= 255) {
            req_bytes = 1;
        } else if(ub <= 65535) {
            req_bytes = 2;
        } else if(ub <= 4294967295UL) {
            req_bytes = 4;
        } else if(ub <= 18446744073709551615ULL) {
            req_bytes = 8;
        }
    } else if(ct
              && ((ct->flags
                  & (AOC_HAS_LOWER_BOUND | AOC_HAS_UPPER_BOUND))
                        == (AOC_HAS_LOWER_BOUND | AOC_HAS_UPPER_BOUND))) {
        /* X.969 08/2015 10.2(b) - no lower bound or negative lower bound */

        intmax_t lb = ct->lower_bound;
        intmax_t ub = ct->upper_bound;

        if(lb >= -128 && ub <= 127) {
            req_bytes = 1;
        } else if(lb >= -32768 && ub <= 32767) {
            req_bytes = 2;
        } else if(lb >= -2147483648L && ub <= 2147483647L) {
            req_bytes = 4;
        } else if(lb >= -9223372036854775808LL && ub <= 9223372036854775807LL) {
            req_bytes = 8;
        }
    }

    if(req_bytes == 0) {
        ssize_t r = oer_serialize_length(useful_bytes, cb, app_key);
        if(r < 0) {
            ASN__ENCODE_FAILED;
        }
        er.encoded += r;
        req_bytes = useful_bytes;
    } else if(req_bytes < useful_bytes) {
        ASN__ENCODE_FAILED;
    }

    er.encoded += req_bytes;

    for(; req_bytes > useful_bytes; req_bytes--) {
        if(cb(sign?"\xff":"\0", 1, app_key) < 0) {
            ASN__ENCODE_FAILED;
        }
    }

    if(cb(buf, useful_bytes, app_key) < 0) {
        ASN__ENCODE_FAILED;
    }

    ASN__ENCODED_OK(er);
}

#endif  /* ASN_DISABLE_OER_SUPPORT */