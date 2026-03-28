/* POSITIVE TEST: Linked list with pointer traversal.
 * Exercises: self-referential structs, pointer comparison to null,
 * arrow operator, address-of, while loop with pointer iteration.
 *
 * EXPECTED: compile success
 * STATUS: PASSES
 */

struct node {
    int value;
    struct node *next;
};

int list_sum(struct node *head) {
    int sum = 0;
    struct node *cur;
    cur = head;
    while (cur != 0) {
        sum = sum + cur->value;
        cur = cur->next;
    }
    return sum;
}

int main(void) {
    struct node a;
    struct node b;
    struct node c;
    a.value = 10;
    b.value = 20;
    c.value = 12;
    a.next = &b;
    b.next = &c;
    c.next = 0;
    return list_sum(&a);
}
