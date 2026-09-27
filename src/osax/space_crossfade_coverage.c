// Union area of a bounded set of clipped rectangles. A sweep over x edges
// merges the y intervals in each strip; overlapping windows count once.
#define SPACE_CROSSFADE_RECTS 128

static int space_crossfade_compare_coordinate(const void *a, const void *b)
{
    double left = *(const double *) a;
    double right = *(const double *) b;
    return (left > right) - (left < right);
}

static int space_crossfade_compare_y(const void *a, const void *b)
{
    const CGRect *left = a;
    const CGRect *right = b;
    return (left->origin.y > right->origin.y) - (left->origin.y < right->origin.y);
}

static double space_crossfade_covered_area(CGRect *rects, int count)
{
    if (!count) return 0.0;

    double edges[2 * SPACE_CROSSFADE_RECTS];
    for (int i = 0; i < count; ++i) {
        edges[2 * i] = CGRectGetMinX(rects[i]);
        edges[2 * i + 1] = CGRectGetMaxX(rects[i]);
    }

    qsort(edges, 2 * count, sizeof(*edges), space_crossfade_compare_coordinate);
    qsort(rects, count, sizeof(*rects), space_crossfade_compare_y);
    double area = 0.0;

    for (int x = 1; x < 2 * count; ++x) {
        double width = edges[x] - edges[x - 1];
        if (!(width > 0.0)) continue;

        double height = 0.0;
        double bottom = -INFINITY;
        for (int i = 0; i < count; ++i) {
            if (CGRectGetMinX(rects[i]) >= edges[x] || CGRectGetMaxX(rects[i]) <= edges[x - 1]) continue;

            double top = fmax(CGRectGetMinY(rects[i]), bottom);
            double end = CGRectGetMaxY(rects[i]);
            if (end > top) height += end - top;
            if (end > bottom) bottom = end;
        }

        area += width * height;
    }

    return area;
}
