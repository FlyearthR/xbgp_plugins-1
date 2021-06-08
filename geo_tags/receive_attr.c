//
// Created by thomas on 19/02/20.
//

#include <stdint.h>
#include "router_bgp_config.h"
#include "../xbgp_compliant_api/xbgp_plugin_api.h"
#include <bytecode_public.h>

#include "../prove_stuffs/prove.h"

#ifdef PROVERS
uint8_t get_uint8(void);
uint64_t get_uint64(void);

void *get_arg(unsigned int id) {
    switch (id) {
        case ARG_CODE:
        case ARG_FLAGS:{
            uint8_t *code;
            code = malloc(sizeof(*code));
            *code =  get_uint8();
            return code;
        }
        case ARG_LENGTH: {
            uint16_t *length; // by chance ORIGINATOR and GEO are on hte same length
            length = malloc((sizeof(*length)));
            *length = 8;
            return length;
        }
        case ARG_DATA: {
            uint64_t *data = malloc(sizeof(uint64_t));
            *data = get_uint64();
            return data;
        }
        default:
            return NULL;
    }
}

int add_attr(uint8_t code, uint8_t flags, uint16_t length, uint8_t *decoded_attr) {

    p_assert(code == PREFIX_ORIGINATOR || code == BA_GEO_TAG);

    uint8_t minibuf[5];

    // i < 4096 limits the unrolling of loops
    // 4096 is the upper bound for BGP messages
    for (int i = 0; i < length && i < 4096; i++) {
        minibuf[i % 5] = minibuf[(i - 1) % 5] + decoded_attr[i];
    }
    return 0;
}
#endif

static __always_inline unsigned int is_negative(uint32_t number) {
    return ((number & 0xffffffff) >> 31u) & 1u;
}

static __always_inline int32_t decode(uint32_t number) {
    if (!is_negative(number)) return number;
    return -(number & 0x7fffffffu);
}


static __always_inline int decode_attr(uint8_t code, uint16_t len, uint32_t flags, const uint8_t *data) {
    struct ubpf_peer_info *pinfo;

    switch (code) {
        case PREFIX_ORIGINATOR:
            pinfo = get_src_peer_info();
            if (!pinfo) {
                ebpf_print("Unable to get peer info !\n");
                return -1;
            }

            if (pinfo->peer_type == EBGP_SESSION) return -1;
            /* fallthrough */
        case BA_GEO_TAG: {
            if (len != 8) return -1; // malformed attribute

            uint32_t raw_latitude;
            uint32_t raw_longitude;

            int32_t geo_tag[2];
            uint64_t *attr_data;
            attr_data = (uint64_t *) geo_tag;

            raw_latitude = *((uint32_t *) data);
            data += 4;
            raw_longitude = *((uint32_t *) data);

            raw_latitude = ebpf_ntohl(raw_latitude);
            raw_longitude = ebpf_ntohl(raw_longitude);

            geo_tag[0] = decode(raw_latitude);
            geo_tag[1] = decode(raw_longitude);

            return add_attr(code, flags, len, (uint8_t *) attr_data) == -1 ? -1 : 0;
        }
        default:
            return -1;
    }
    return 0;
}

/**
 * Decode a given attribute passed by the protocol
 * @param args contains the current attribute
 * @return EXIT_SUCCESS if the attribute has been decoded and stored in the protocol memory
 *         EXIT_FAILURE otherwise. The protocol itself must decode this attribute.
 */
uint64_t generic_decode_attr(args_t *args UNUSED) {

    uint8_t *code;
    uint16_t *len;
    uint8_t *flags;
    uint8_t *data;

    code = get_arg(ARG_CODE);
    flags = get_arg(ARG_FLAGS);
    data = get_arg(ARG_DATA);
    len = get_arg(ARG_LENGTH);

    if (!code || !len || !flags || !data) {
        return EXIT_FAILURE;
    }

    return decode_attr(*code, *len, *flags, data) == -1 ? EXIT_FAILURE : EXIT_SUCCESS;
}