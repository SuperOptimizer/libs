/* EXPECTED: 0 */
/* Linked list traversal to test pointer load latency */
struct node {
    int value;
    struct node *next;
};

struct node nodes[10000];

int main(void) {
    int i;
    int sum = 0;
    struct node *p;

    /* Build linked list in scrambled order using a simple permutation */
    for (i = 0; i < 10000; i++) {
        nodes[i].value = i;
    }
    /* Link in strided order: 0 -> 7 -> 14 -> ... wrapping */
    for (i = 0; i < 10000; i++) {
        nodes[i].next = &nodes[(i * 7 + 1) % 10000];
    }

    /* Traverse 100000 steps */
    p = &nodes[0];
    for (i = 0; i < 100000; i++) {
        sum += p->value;
        p = p->next;
    }

    /* Verify traversal produced a result */
    return (sum > 0) ? 0 : 1;
}
