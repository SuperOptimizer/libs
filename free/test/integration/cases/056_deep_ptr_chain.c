/* EXPECTED: 42 */
/* Test: deep pointer chain p->next->next->val */
struct Node { struct Node *next; int val; };

int main(void) {
    struct Node a, b, c;
    c.next = 0;
    c.val = 42;
    b.next = &c;
    b.val = 0;
    a.next = &b;
    a.val = 0;
    return a.next->next->val;
}
