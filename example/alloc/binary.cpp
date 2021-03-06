#include <iostream>
#include <nvheap.h>


using namespace std;

struct node {
    int key;
    NVPtr left, right;
} node;

// A utility function to do inorder traversal of BST
void inorder(struct node* root)
{
    if (root != NULL)
    {
        inorder((struct node*) root->left.dptr());
        cout << root->key << endl;
        inorder((struct node*) root->right.dptr());
    }
}

/* A utility function to insert a new node with given key in BST */
struct node* insert(struct node* node, int key)
{
    /* If the tree is empty, return a new node */
    if (node == NULL) {
        struct node *temp =  (struct node *)nvh_malloc(sizeof(struct node));
        temp->key = key;
        temp->left = NULL;
        temp->right = NULL;
        node = temp;
    }
    else {
        /* Otherwise, recur down the tree */
        if (key < node->key)
            node->left  = insert((struct node*) node->left.dptr(), key);
        else if (key > node->key)
            node->right = insert((struct node*) node->right.dptr(), key);
    }
    int c, d;
for (c = 1; c <= 32767; c++)
for (d = 1; d <= 32767; d++)
{}
    nvh_set_root(node);
    /* return the (unchanged) node pointer */
    return node;
}


int main(int argc, char* argv[])
{
    ios_base::sync_with_stdio(false);
    if (nvh_init(argv[1], argv[2]) < 0) {
        cerr << "Failed to initialize NV-heap\n";
        exit(1);
    }

    struct node *root = (struct node *) nvh_get_root();
    char option;
    int val, run = 1;
    cout << "Insert: i val, Print: p, Quit: q\n";
    while (run) {
        cin >> option;
        switch (option)
        {
            case ('q'):
            case ('Q'):
            run = 0;
            break;
            case ('i'):     //Insert
            case ('I'):
            cin >> val;
            tx_begin();
            root = insert (root, val);
            tx_commit();
            cout << "Inserted " << val << endl;
            break;
            case ('p'):
            case ('P'):
            inorder(root);
            break;
        }
    }
    return 0;
}
