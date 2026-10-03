// BSP node updates, traversal, balancing and transformations.
// Runs on the event-loop thread.

static inline bool window_node_is_occupied(struct window_node *node)
{
    return node->window_count != 0;
}

static inline bool window_node_is_intermediate(struct window_node *node)
{
    return node->parent != NULL;
}

static inline bool window_node_is_leaf(struct window_node *node)
{
    return node->left == NULL && node->right == NULL;
}

static inline bool window_node_is_left_child(struct window_node *node)
{
    return node->parent && node->parent->left == node;
}

static inline bool window_node_is_right_child(struct window_node *node)
{
    return node->parent && node->parent->right == node;
}

static void window_node_equalize(struct window_node *node, uint32_t axis_flag)
{
    if (node->left)  window_node_equalize(node->left, axis_flag);
    if (node->right) window_node_equalize(node->right, axis_flag);

    if ((axis_flag & SPLIT_Y) && node->split == SPLIT_Y) {
        node->ratio = g_space_manager.split_ratio;
    }

    if ((axis_flag & SPLIT_X) && node->split == SPLIT_X) {
        node->ratio = g_space_manager.split_ratio;
    }
}

static inline struct balance_node balance_node_add(struct balance_node a, struct balance_node b)
{
    return (struct balance_node) { a.y_count + b.y_count, a.x_count + b.x_count, };
}

static struct balance_node window_node_balance(struct window_node *node, uint32_t axis_flag)
{
    if (window_node_is_leaf(node)) {
        return (struct balance_node) {
            node->parent ? node->parent->split == SPLIT_Y : 0,
            node->parent ? node->parent->split == SPLIT_X : 0
        };
    }

    struct balance_node left_leafs  = window_node_balance(node->left, axis_flag);
    struct balance_node right_leafs = window_node_balance(node->right, axis_flag);
    struct balance_node total_leafs = balance_node_add(left_leafs, right_leafs);

    if (axis_flag & SPLIT_Y) {
        if (node->split == SPLIT_Y) {
            node->ratio = (float) left_leafs.y_count / total_leafs.y_count;
            --total_leafs.y_count;
        }
    }

    if (axis_flag & SPLIT_X) {
        if (node->split == SPLIT_X) {
            node->ratio = (float) left_leafs.x_count / total_leafs.x_count;
            --total_leafs.x_count;
        }
    }

    if (node->parent) {
        total_leafs.y_count += node->parent->split == SPLIT_Y;
        total_leafs.x_count += node->parent->split == SPLIT_X;
    }

    return total_leafs;
}

static void window_node_split(struct view *view, struct window_node *node, struct window *window)
{
    struct window_node *left = malloc(sizeof(struct window_node));
    memset(left, 0, sizeof(struct window_node));

    struct window_node *right = malloc(sizeof(struct window_node));
    memset(right, 0, sizeof(struct window_node));

    struct window_node *zoom = !g_space_manager.window_zoom_persist
                             ? NULL
                             : !node->zoom
                             ? NULL
                             : node->zoom == node->parent
                             ? node
                             : view->root;

    if (window_node_get_child(node) == CHILD_SECOND) {
        memcpy(left->window_list, node->window_list, sizeof(uint32_t) * node->window_count);
        memcpy(left->window_order, node->window_order, sizeof(uint32_t) * node->window_count);
        left->window_count = node->window_count;
        left->zoom = zoom;

        right->window_list[0] = window->id;
        right->window_order[0] = window->id;
        right->window_count = 1;
    } else {
        memcpy(right->window_list, node->window_list, sizeof(uint32_t) * node->window_count);
        memcpy(right->window_order, node->window_order, sizeof(uint32_t) * node->window_count);
        right->window_count = node->window_count;
        right->zoom = zoom;

        left->window_list[0] = window->id;
        left->window_order[0] = window->id;
        left->window_count = 1;
    }

    left->parent  = node;
    right->parent = node;

    node->window_count = 0;
    node->left  = left;
    node->right = right;
    node->zoom  = NULL;

    area_make_pair_for_node(view, node);
}

void window_node_update(struct view *view, struct window_node *node)
{
    if (window_node_is_leaf(node)) {
        if (node->insert_dir) insert_feedback_show(node);
    } else {
        area_make_pair_for_node(view, node);
        window_node_update(view, node->left);
        window_node_update(view, node->right);
    }
}

static void window_node_destroy(struct window_node *node)
{
    if (node->left)  window_node_destroy(node->left);
    if (node->right) window_node_destroy(node->right);

    for (int i = 0; i < node->window_count; ++i) {
        window_manager_remove_managed_window(&g_window_manager, node->window_list[i]);
    }

    insert_feedback_destroy(node);
    free(node);
}

static void window_node_clear_zoom(struct window_node *node)
{
    node->zoom = NULL;

    if (!window_node_is_leaf(node)) {
        window_node_clear_zoom(node->left);
        window_node_clear_zoom(node->right);
    }
}

void window_node_capture_windows(struct window_node *node, struct window_capture **window_list)
{
    if (window_node_is_leaf(node)) {
        for (int i = 0; i < node->window_count; ++i) {
            struct window *window = window_manager_find_window(&g_window_manager, node->window_list[i]);
            if (window) {
                struct area area = node->zoom ? node->zoom->area : node->area;
                ts_buf_push(*window_list, ((struct window_capture) { .window = window, .x = area.x, .y = area.y, .w = area.w, .h = area.h }));
            }
        }
    } else {
        window_node_capture_windows(node->left, window_list);
        window_node_capture_windows(node->right, window_list);
    }
}

void window_node_flush(struct window_node *node)
{
    struct window_capture *window_list = NULL;
    window_node_capture_windows(node, &window_list);
    if (window_list) window_manager_animate_window_list(window_list, ts_buf_len(window_list));
}

bool window_node_contains_window(struct window_node *node, uint32_t window_id)
{
    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_list[i] == window_id) return true;
    }

    return false;
}

int window_node_index_of_window(struct window_node *node, uint32_t window_id)
{
    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_list[i] == window_id) return i;
    }

    return 0;
}

void window_node_swap_window_list(struct window_node *a_node, struct window_node *b_node)
{
    uint32_t tmp_window_list[NODE_MAX_WINDOW_COUNT];
    uint32_t tmp_window_order[NODE_MAX_WINDOW_COUNT];
    uint32_t tmp_window_count;

    memcpy(tmp_window_list, a_node->window_list, sizeof(uint32_t) * a_node->window_count);
    memcpy(tmp_window_order, a_node->window_order, sizeof(uint32_t) * a_node->window_count);
    tmp_window_count = a_node->window_count;

    memcpy(a_node->window_list, b_node->window_list, sizeof(uint32_t) * b_node->window_count);
    memcpy(a_node->window_order, b_node->window_order, sizeof(uint32_t) * b_node->window_count);
    a_node->window_count = b_node->window_count;

    memcpy(b_node->window_list, tmp_window_list, sizeof(uint32_t) * tmp_window_count);
    memcpy(b_node->window_order, tmp_window_order, sizeof(uint32_t) * tmp_window_count);
    b_node->window_count = tmp_window_count;

    a_node->zoom = NULL;
    b_node->zoom = NULL;
}

struct window_node *window_node_find_first_leaf(struct window_node *root)
{
    struct window_node *node = root;
    while (!window_node_is_leaf(node)) {
        node = node->left;
    }
    return node;
}

struct window_node *window_node_find_last_leaf(struct window_node *root)
{
    struct window_node *node = root;
    while (!window_node_is_leaf(node)) {
        node = node->right;
    }
    return node;
}

struct window_node *window_node_find_prev_leaf(struct window_node *node)
{
    if (!node->parent) return NULL;

    if (window_node_is_left_child(node)) {
        return window_node_find_prev_leaf(node->parent);
    }

    if (window_node_is_leaf(node->parent->left)) {
        return node->parent->left;
    }

    return window_node_find_last_leaf(node->parent->left->right);
}

struct window_node *window_node_find_next_leaf(struct window_node *node)
{
    if (!node->parent) return NULL;

    if (window_node_is_right_child(node)) {
        return window_node_find_next_leaf(node->parent);
    }

    if (window_node_is_leaf(node->parent->right)) {
        return node->parent->right;
    }

    return window_node_find_first_leaf(node->parent->right->left);
}

void window_node_rotate(struct window_node *node, int degrees)
{
    if ((degrees ==  90 && node->split == SPLIT_Y) ||
        (degrees == 270 && node->split == SPLIT_X) ||
        (degrees == 180)) {
        struct window_node *temp = node->left;
        node->left  = node->right;
        node->right = temp;
        node->ratio = 1 - node->ratio;
    }

    if (degrees != 180) {
        if (node->split == SPLIT_X) {
            node->split = SPLIT_Y;
        } else if (node->split == SPLIT_Y) {
            node->split = SPLIT_X;
        }
    }

    if (!window_node_is_leaf(node)) {
        window_node_rotate(node->left, degrees);
        window_node_rotate(node->right, degrees);
    }
}

struct window_node *window_node_mirror(struct window_node *node, enum window_node_split axis)
{
    if (!window_node_is_leaf(node)) {
        struct window_node *left = window_node_mirror(node->left, axis);
        struct window_node *right = window_node_mirror(node->right, axis);

        if (node->split == axis) {
            node->left = right;
            node->right = left;
        }
    }

    return node;
}

struct window_node *window_node_fence(struct window_node *node, int dir)
{
    if (!node) return NULL;

    for (struct window_node *parent = node->parent; parent; parent = parent->parent) {
        if ((dir == DIR_NORTH && parent->split == SPLIT_X && parent->area.y < node->area.y) ||
            (dir == DIR_WEST  && parent->split == SPLIT_Y && parent->area.x < node->area.x) ||
            (dir == DIR_SOUTH && parent->split == SPLIT_X && (parent->area.y + parent->area.h) > (node->area.y + node->area.h)) ||
            (dir == DIR_EAST  && parent->split == SPLIT_Y && (parent->area.x + parent->area.w) > (node->area.x + node->area.w))) {
                return parent;
        }
    }

    return NULL;
}

static struct window_node *view_find_min_depth_leaf_node(struct window_node *node)
{
    int capacity = 256;
    int count = 1;
    struct window_node **list = ts_alloc_list(struct window_node *, capacity);
    list[0] = node;

    for (int i = 0; i < count; ++i) {
        if (window_node_is_leaf(list[i])) {
            return list[i];
        }

        if (count + 2 > capacity) {
            ts_expand(list,
                      sizeof(struct window_node *) * capacity,
                      sizeof(struct window_node *) * capacity);
            capacity *= 2;
        }

        list[count++] = list[i]->left;
        list[count++] = list[i]->right;
    }

    return NULL;
}
