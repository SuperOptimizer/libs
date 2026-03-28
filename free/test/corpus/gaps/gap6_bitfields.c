/* GAP-6: Bitfield syntax not parsed
 * EXPECTED: compile success
 * STATUS: FAILS - error: expected ';', got token kind 47
 *
 * The parser does not handle the ':' bitfield width syntax in struct
 * member declarations. Used in embedded code, protocol parsers, and
 * hardware register definitions.
 */

struct flags {
    unsigned int readable : 1;
    unsigned int writable : 1;
    unsigned int executable : 1;
    unsigned int reserved : 5;
};

struct ip_header {
    unsigned int version : 4;
    unsigned int ihl : 4;
    unsigned int dscp : 6;
    unsigned int ecn : 2;
};

int main(void) {
    struct flags f;
    f.readable = 1;
    f.writable = 0;
    f.executable = 1;
    return (int)f.readable;
}
