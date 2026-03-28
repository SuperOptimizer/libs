/* EXPECTED: 42 */
struct Node {
    int val;
    struct Node *next;
};

int main(void) {
    struct Node a, b, c;
    a.val = 10;
    a.next = &b;
    b.val = 14;
    b.next = &c;
    c.val = 18;
    c.next = 0;
    return a.next->next->val + a.next->val + a.val;
}
