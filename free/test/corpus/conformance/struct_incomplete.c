/* EXPECTED: 42 */
/* forward declaration of struct, completed later */
struct fwd;

struct fwd {
    int val;
};

int main(void) {
    struct fwd f;
    f.val = 42;
    return f.val;
}
