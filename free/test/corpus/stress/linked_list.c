/* EXPECTED: 55 */
struct node {
    int val;
    struct node *next;
};

int main(void) {
    struct node nodes[10];
    struct node *head;
    struct node *cur;
    int i, sum;

    /* Build linked list: 1 -> 2 -> ... -> 10 */
    for (i = 0; i < 10; i++) {
        nodes[i].val = i + 1;
        if (i < 9) {
            nodes[i].next = &nodes[i + 1];
        } else {
            nodes[i].next = (struct node *)0;
        }
    }
    head = &nodes[0];

    /* Traverse and sum */
    sum = 0;
    cur = head;
    while (cur != (struct node *)0) {
        sum = sum + cur->val;
        cur = cur->next;
    }
    /* 1+2+...+10 = 55 */
    return sum;
}
