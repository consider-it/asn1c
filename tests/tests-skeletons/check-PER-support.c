#include <assert.h>
#include <per_support.h>

static void put(asn_per_outp_t *po, size_t length) {
    fprintf(stderr, "put(%zd)\n", length);
    do {
        int need_eom = 123;
        ssize_t may_write = uper_put_length(po, length, &need_eom);
        fprintf(stderr, "  put %zu\n", may_write);
        assert(may_write >= 0);
        assert((size_t)may_write <= length);
        assert(need_eom != 123);
        length -= may_write;
        if(need_eom) {
            assert(length == 0);
            if(uper_put_length(po, 0, 0)) {
                assert(!"Unreachable");
            }
            fprintf(stderr, "  put EOM 0\n");
        }
    } while(length);
    fprintf(stderr, "put(...) in %zu bits\n", po->nboff);
    assert(po->nboff != 0);
    assert(po->flushed_bytes == 0);
}

static size_t get(asn_per_outp_t *po) {
    asn_bit_data_t data;
    memset(&data, 0, sizeof(data));
    data.buffer = po->tmpspace;
    data.nboff = 0;
    data.nbits = 8 * (po->buffer - po->tmpspace) + po->nboff;

    fprintf(stderr, "get(): %s\n", asn_bit_data_string(&data));

    size_t length = 0;
    int repeat = 0;
    do {
        ssize_t n = uper_get_length(&data, -1, 0, &repeat);
        fprintf(stderr, "  get = %zu +%zd\n", length, n);
        assert(n >= 0);
        length += n;
    } while(repeat);
    fprintf(stderr, "get() = %zu\n", length);

    return length;
}

static void
check_round_trip(size_t length) {
    fprintf(stderr, "\nRound-trip for %zu\n", length);
    asn_per_outp_t po;

    memset(&po, 0, sizeof(po));
    po.buffer = po.tmpspace;
    po.nbits = 8 * sizeof(po.tmpspace);

    put(&po, length);
    size_t recovered = get(&po);

    assert(recovered == length);
}

/*
 * Checks that we can get the PER length that we have just put,
 * and receive the same value.
 */
static void
check_round_trips() {
    check_round_trip(0);
    check_round_trip(1);
    check_round_trip(127);
    check_round_trip(128);
    check_round_trip(129);
    check_round_trip(255);
    check_round_trip(256);
    check_round_trip(65534);
    check_round_trip(65535);
    check_round_trip(65536);
    check_round_trip(65538);
    check_round_trip(128000);
    for(size_t i = 1; i < 10; i++) {
        check_round_trip(i*16384 - 1);
        check_round_trip(i*16384);
        check_round_trip(i*16384 + 1);
    }
}

#define OK_UNREBASE(w, l, u, c) check_unrebase(__LINE__, 0, w, l, u, c)
#define NO_UNREBASE(w, l, u) check_unrebase(__LINE__, 1, w, l, u, 0)

static void
check_unrebase(int lineno, int expected_to_fail, uint64_t wire_value,
               int64_t lb, int64_t ub, int64_t control) {
    fprintf(stderr, "%03d: Checking recovery of %"PRIu64" (%"PRIi64"..%"PRIi64")", lineno,
            wire_value, lb, ub);
    if(expected_to_fail) {
        fprintf(stderr, " to FAIL\n");
    } else {
        fprintf(stderr, " into %"PRIi64"\n", control);
    }

    int64_t outcome;
    int ret = per_long_range_unrebase(wire_value, lb, ub, &outcome);
    if(ret == 0) {
        assert(!expected_to_fail);
    } else {
        assert(expected_to_fail);
        return;
    }

    assert(outcome == control);
}


#define OK_REBASE_ROUNDTRIP(v, l, u) \
    check_range_rebase_round_trip(__LINE__, 0, v, l, u)

#define NO_REBASE_ROUNDTRIP(v, l, u) \
    check_range_rebase_round_trip(__LINE__, 1, v, l, u)

static void
check_range_rebase_round_trip(int lineno, int expected_to_fail, int64_t value,
                              int64_t lb, int64_t ub) {
    uint64_t wire_value;
    int ret;

    fprintf(stderr, "%03d: Rebase %"PRIi64" into (%"PRIi64"..%"PRIi64") %s\n", lineno, value, lb,
            ub, expected_to_fail ? "FAIL" : "OK");

    ret = per_long_range_rebase(value, lb, ub, &wire_value);
    if(ret != 0) {
        if(expected_to_fail) {
            return;
        } else {
            fprintf(stderr, "%03d: Original %"PRIi64" (%"PRIi64"..%"PRIi64") failed to rebase\n",
                    lineno, value, lb, ub);
            assert(ret == 0);
        }
    } if(expected_to_fail) {
        fprintf(
            stderr,
            "%03d: Original %"PRIi64" (%"PRIi64"..%"PRIi64") rebased to %"PRIu64" where it shouldn't\n",
            lineno, value, lb, ub, wire_value);
        assert(expected_to_fail && ret == -1);
    }

    fprintf(stderr, "%03d: Recover %"PRIu64" into (%"PRIi64"..%"PRIi64")\n", lineno,
            wire_value, lb, ub);

    int64_t recovered;
    ret = per_long_range_unrebase(wire_value, lb, ub, &recovered);
    if(ret != 0) {
        fprintf(stderr, "%03d: Wire value %"PRIu64" (%"PRIi64"..%"PRIi64") failed to unrebase\n",
                lineno, wire_value, lb, ub);
        assert(ret == 0);
    }

    if(value != recovered) {
        fprintf(stderr,
                "%03d: Value %"PRIi64" (%"PRIi64"..%"PRIi64") failed to round-trip (=%"PRIi64")\n",
                lineno, value, lb, ub, recovered);
        assert(value == recovered);
    }

}

static void
check_range_rebase() {

    OK_UNREBASE(0U, 0, 0, 0);
    NO_UNREBASE(1U, 0, 0);
    OK_UNREBASE(0, LONG_MAX, LONG_MAX, LONG_MAX);
    NO_UNREBASE(1, LONG_MAX, LONG_MAX);
    OK_UNREBASE(0, LONG_MAX-1, LONG_MAX-1, LONG_MAX-1);
    NO_UNREBASE(1, LONG_MAX-1, LONG_MAX-1);

    OK_REBASE_ROUNDTRIP(0, 0, 0);
    OK_REBASE_ROUNDTRIP(0, 0, 1);
    OK_REBASE_ROUNDTRIP(1, 0, 1);

    NO_REBASE_ROUNDTRIP(-1, 0, 0);
    NO_REBASE_ROUNDTRIP(1, 0, 0);
    NO_REBASE_ROUNDTRIP(LONG_MIN, 0, 0);
    NO_REBASE_ROUNDTRIP(LONG_MAX, 0, 0);

    OK_REBASE_ROUNDTRIP(-2, -2, -1);
    OK_REBASE_ROUNDTRIP(-1, -2, -1);
    NO_REBASE_ROUNDTRIP(-3, -2, -1);
    NO_REBASE_ROUNDTRIP(0, -2, -1);

    OK_REBASE_ROUNDTRIP(LONG_MAX, LONG_MAX, LONG_MAX);
    NO_REBASE_ROUNDTRIP(LONG_MAX-1, LONG_MAX, LONG_MAX);
    NO_REBASE_ROUNDTRIP(0, LONG_MAX, LONG_MAX);
    NO_REBASE_ROUNDTRIP(LONG_MIN, LONG_MAX, LONG_MAX);
    NO_REBASE_ROUNDTRIP(LONG_MIN+1, LONG_MAX, LONG_MAX);

    OK_REBASE_ROUNDTRIP(LONG_MIN, LONG_MIN, LONG_MIN);
    NO_REBASE_ROUNDTRIP(LONG_MIN+1, LONG_MIN, LONG_MIN);
    NO_REBASE_ROUNDTRIP(0, LONG_MIN, LONG_MIN);
    NO_REBASE_ROUNDTRIP(LONG_MAX-1, LONG_MIN, LONG_MIN);
    NO_REBASE_ROUNDTRIP(LONG_MAX, LONG_MIN, LONG_MIN);
    OK_REBASE_ROUNDTRIP(LONG_MAX-10, LONG_MAX-10, LONG_MAX-5);
    OK_REBASE_ROUNDTRIP(LONG_MAX-5, LONG_MAX-10, LONG_MAX-5);
    OK_REBASE_ROUNDTRIP(LONG_MAX-7, LONG_MAX-10, LONG_MAX-5);
    NO_REBASE_ROUNDTRIP(LONG_MAX-4, LONG_MAX-10, LONG_MAX-5);

    NO_REBASE_ROUNDTRIP(LONG_MIN, LONG_MIN+1, LONG_MIN+2);
    OK_REBASE_ROUNDTRIP(LONG_MIN+1, LONG_MIN+1, LONG_MIN+2);
    OK_REBASE_ROUNDTRIP(LONG_MIN+2, LONG_MIN+1, LONG_MIN+2);
    NO_REBASE_ROUNDTRIP(LONG_MIN+3, LONG_MIN+1, LONG_MIN+2);
    OK_REBASE_ROUNDTRIP(LONG_MIN, LONG_MIN, LONG_MIN+1);
    OK_REBASE_ROUNDTRIP(LONG_MIN+1, LONG_MIN, 0);
    OK_REBASE_ROUNDTRIP(LONG_MIN+1, LONG_MIN, LONG_MIN+1);
    NO_REBASE_ROUNDTRIP(LONG_MIN+2, LONG_MIN, LONG_MIN+1);

    OK_REBASE_ROUNDTRIP(-1, -1, 1);
    OK_REBASE_ROUNDTRIP(0, -1, 1);
    OK_REBASE_ROUNDTRIP(1, -1, 1);

    NO_REBASE_ROUNDTRIP(-2, -1, 1);
    NO_REBASE_ROUNDTRIP(2, -1, 1);
    NO_REBASE_ROUNDTRIP(LONG_MIN, -1, 1);
    NO_REBASE_ROUNDTRIP(LONG_MAX, -1, 1);

    OK_REBASE_ROUNDTRIP(-1, LONG_MIN, LONG_MAX);
    OK_REBASE_ROUNDTRIP(0, LONG_MIN, LONG_MAX);
    OK_REBASE_ROUNDTRIP(1, LONG_MIN, LONG_MAX);
    OK_REBASE_ROUNDTRIP(LONG_MIN, LONG_MIN, LONG_MAX);
    OK_REBASE_ROUNDTRIP(LONG_MAX, LONG_MIN, LONG_MAX);

#ifndef  EXPLICIT_32BIT
    if(sizeof(long) == 8) {
        OK_REBASE_ROUNDTRIP(0, LONG_MIN, LONG_MAX);

        /* Too wide range, would not fit uint32_t */
        OK_REBASE_ROUNDTRIP(INT32_MIN, (long)INT32_MIN - 1,
                            (long)INT32_MAX + 1);
        OK_REBASE_ROUNDTRIP(INT32_MAX, (long)INT32_MIN - 1,
                            (long)INT32_MAX + 1);
        OK_REBASE_ROUNDTRIP((long)INT32_MIN - 1, (long)INT32_MIN - 1,
                            (long)INT32_MAX + 1);
        OK_REBASE_ROUNDTRIP((long)INT32_MAX + 1, (long)INT32_MIN - 1,
                            (long)INT32_MAX + 1);
        NO_REBASE_ROUNDTRIP(LONG_MIN, (long)INT32_MIN - 1,
                            (long)INT32_MAX + 1);
        NO_REBASE_ROUNDTRIP(LONG_MAX, (long)INT32_MIN - 1,
                            (long)INT32_MAX + 1);

        NO_REBASE_ROUNDTRIP(((long)INT32_MIN)-1, INT32_MIN, INT32_MAX);
        NO_REBASE_ROUNDTRIP(((long)INT32_MAX)+1, INT32_MIN, INT32_MAX);
        NO_REBASE_ROUNDTRIP(LONG_MIN, INT32_MIN, INT32_MAX);
        NO_REBASE_ROUNDTRIP(LONG_MAX, INT32_MIN, INT32_MAX);
    }
#endif

    OK_REBASE_ROUNDTRIP(-1, LONG_MIN + 1, LONG_MAX - 1);
    OK_REBASE_ROUNDTRIP(0, LONG_MIN + 1, LONG_MAX - 1);
    OK_REBASE_ROUNDTRIP(-1, LONG_MIN + 1, LONG_MAX - 1);
    OK_REBASE_ROUNDTRIP(LONG_MIN + 1, LONG_MIN + 1, LONG_MAX - 1);
    OK_REBASE_ROUNDTRIP(LONG_MAX - 1, LONG_MIN + 1, LONG_MAX - 1);
    NO_REBASE_ROUNDTRIP(LONG_MIN, LONG_MIN + 1, LONG_MAX - 1);
    NO_REBASE_ROUNDTRIP(LONG_MAX, LONG_MIN + 1, LONG_MAX - 1);

    if(sizeof(long) == 8) {
        NO_REBASE_ROUNDTRIP(LONG_MIN, INT32_MIN + 1, INT32_MAX - 1);
        NO_REBASE_ROUNDTRIP(LONG_MAX, INT32_MIN + 1, INT32_MAX - 1);
    }
}

int main() {

    check_range_rebase();
    check_round_trips();

}

