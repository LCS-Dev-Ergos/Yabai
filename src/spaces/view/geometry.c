// Area conversion and geometry for BSP splits.
// Runs on the event-loop thread.

struct area area_from_cgrect(CGRect rect)
{
    return (struct area) { rect.origin.x, rect.origin.y, rect.size.width, rect.size.height };
}

CGPoint area_max_point(struct area area)
{
    return (CGPoint) { area.x + area.w - 1, area.y + area.h - 1 };
}

static inline enum window_node_child window_node_get_child(struct window_node *node)
{
    return node->child != CHILD_NONE ? node->child : g_space_manager.window_placement;
}

static inline enum window_node_split window_node_get_split(struct view *view, struct window_node *node)
{
    if (node->split != SPLIT_NONE) return node->split;

    if (view->split_type != SPLIT_NONE) {
        if (view->split_type != SPLIT_AUTO) {
            return view->split_type;
        }
    } else if (g_space_manager.split_type != SPLIT_AUTO) {
        return g_space_manager.split_type;
    }

    return node->area.w >= node->area.h ? SPLIT_Y : SPLIT_X;
}

static inline float window_node_get_ratio(struct window_node *node)
{
    return in_range_ii(node->ratio, 0.1f, 0.9f) ? node->ratio : g_space_manager.split_ratio;
}

static inline int window_node_get_gap(struct view *view)
{
    return view_check_flag(view, VIEW_ENABLE_GAP) ? view->window_gap : 0;
}

static void area_make_pair(enum window_node_split split, int gap, float ratio, struct area *parent_area, struct area *left_area, struct area *right_area)
{
    if (split == SPLIT_Y) {
        *left_area  = *parent_area;
        *right_area = *parent_area;

        float left_width  = (parent_area->w - gap) * ratio;
        float right_width = (parent_area->w - gap) * (1 - ratio);

        left_area->w   = (int)left_width;
        right_area->w  = (int)right_width;
        right_area->x += (int)(left_width + 0.5f) + gap;
    } else {
        *left_area  = *parent_area;
        *right_area = *parent_area;

        float left_width  = (parent_area->h - gap) * ratio;
        float right_width = (parent_area->h - gap) * (1 - ratio);

        left_area->h   = (int)left_width;
        right_area->h  = (int)right_width;
        right_area->y += (int)(left_width + 0.5f) + gap;
    }
}

static void area_make_pair_for_node(struct view *view, struct window_node *node)
{
    enum window_node_split split = window_node_get_split(view, node);
    float ratio = window_node_get_ratio(node);
    int gap     = window_node_get_gap(view);

    area_make_pair(split, gap, ratio, &node->area, &node->left->area, &node->right->area);

    node->split = split;
    node->ratio = ratio;
}
