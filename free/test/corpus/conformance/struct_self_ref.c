/* EXPECTED: 30 */
/* struct with pointer to self (linked list node) */
struct node {
    int val;
    struct node *next;
};

int main(void) {
    struct node a, b, c;
    a.val = 10; a.next = &b;
    b.val = 20; b.next = &c;
    c.val = 0;  c.next = 0;
    return a.val + a.next->val; /* 10 + 20 = 30 */
}
