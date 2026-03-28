/* EXPECTED: 15 */
/* function operating on struct with self-pointer (linked list sum) */
struct node {
    int val;
    struct node *next;
};

int sum_list(struct node *head) {
    int total = 0;
    while (head != 0) {
        total += head->val;
        head = head->next;
    }
    return total;
}

int main(void) {
    struct node c, b, a;
    c.val = 3; c.next = 0;
    b.val = 5; b.next = &c;
    a.val = 7; a.next = &b;
    return sum_list(&a); /* 7 + 5 + 3 = 15 */
}
