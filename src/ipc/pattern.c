// Rule and signal patterns: what compiling one costs, bounded before regcomp
// runs on the event loop.

//
// NOTE: regcomp builds a position automaton. It copies out every bounded
// repetition first, so `((a{255}){255}){255}` holds sixteen million positions,
// and it gives each position a transition to every position that can follow
// it, so a run of optional parts such as `a?a?a?...` or `(a|b|c|...)` connects
// every pair. Two thousand `a?` take seconds and gigabytes. We follow the same
// expansion through the pattern's structure, without building anything, and
// refuse a pattern whose automaton would cost more than PATTERN_COST_LIMIT.
//
// A unit is a node, an entry in a node's first or last positions, or a
// transition. A literal costs seven units a character, so the limit admits
// any literal pattern a request can carry, and nothing that costs much more.
//
#define PATTERN_COST_LIMIT (1 << 19)

// Far deeper than any pattern nests; it bounds the parser's stack.
#define PATTERN_MAX_DEPTH 64

struct pattern_cost
{
    double work;
    double transitions;
    double first;
    double last;
    bool nullable;
};

struct pattern_frame
{
    struct pattern_cost alternatives;
    struct pattern_cost branch;
    bool has_alternatives;
    bool has_branch;
};

static double pattern_cost_total(struct pattern_cost cost)
{
    return cost.work + cost.transitions;
}

// A character, a bracket expression or an anchor.
static struct pattern_cost pattern_cost_position(void)
{
    return (struct pattern_cost) { .work = 3, .first = 1, .last = 1 };
}

static struct pattern_cost pattern_cost_empty(void)
{
    return (struct pattern_cost) { .work = 1, .nullable = true };
}

// A node over its children keeps its own first and last positions.
static struct pattern_cost pattern_cost_node(struct pattern_cost cost)
{
    cost.work += 1 + cost.first + cost.last;
    return cost;
}

static struct pattern_cost pattern_cost_concat(struct pattern_cost a, struct pattern_cost b)
{
    return pattern_cost_node((struct pattern_cost) {
        .work        = a.work + b.work,
        .transitions = a.transitions + b.transitions + a.last * b.first,
        .first       = a.first + (a.nullable ? b.first : 0),
        .last        = b.last + (b.nullable ? a.last : 0),
        .nullable    = a.nullable && b.nullable
    });
}

static struct pattern_cost pattern_cost_union(struct pattern_cost a, struct pattern_cost b)
{
    return pattern_cost_node((struct pattern_cost) {
        .work        = a.work + b.work,
        .transitions = a.transitions + b.transitions,
        .first       = a.first + b.first,
        .last        = a.last + b.last,
        .nullable    = a.nullable || b.nullable
    });
}

// A repetition of `atom` from `min` to `max` times, -1 for no upper bound, as
// regcomp expands it: `?`, `*`, `+` and bounds up to 1 stay one node, anything
// else becomes `min` copies followed by either a repeating copy or `max - min`
// nested optional ones.
static struct pattern_cost pattern_cost_repeat(struct pattern_cost atom, int min, int max)
{
    if (min <= 1 && max <= 1) {
        if (max == -1) atom.transitions += atom.last * atom.first;
        atom.nullable = atom.nullable || min == 0;
        return pattern_cost_node(atom);
    }

    if (max != -1 && max < min) max = min;

    struct pattern_cost result = pattern_cost_empty();
    for (int i = 0; i < min && pattern_cost_total(result) <= PATTERN_COST_LIMIT; ++i) {
        result = i ? pattern_cost_concat(result, atom) : atom;
    }

    if (max == -1) {
        result = pattern_cost_concat(result, pattern_cost_repeat(atom, 0, -1));
    } else if (max > min) {
        struct pattern_cost optional = pattern_cost_union(pattern_cost_empty(), atom);
        for (int i = min + 1; i < max && pattern_cost_total(optional) <= PATTERN_COST_LIMIT; ++i) {
            optional = pattern_cost_union(pattern_cost_empty(), pattern_cost_concat(atom, optional));
        }

        result = min ? pattern_cost_concat(result, optional) : optional;
    }

    return result;
}

// A count in a bound; past RE_DUP_MAX, which regcomp refuses, it stops at
// RE_DUP_MAX + 1.
static int pattern_parse_count(const char **cursor)
{
    int count = 0;
    while (isdigit((unsigned char) **cursor)) {
        if (count <= RE_DUP_MAX) count = count * 10 + (**cursor - '0');
        ++*cursor;
    }

    return count > RE_DUP_MAX ? RE_DUP_MAX + 1 : count;
}

// A bound `{m}`, `{m,}` or `{m,n}` at `cursor`. regcomp reads a '{' that no
// digit follows as itself.
static bool pattern_parse_bound(const char **cursor, int *min, int *max)
{
    const char *c = *cursor;
    if (c[0] != '{' || !isdigit((unsigned char) c[1])) return false;

    ++c;
    *min = pattern_parse_count(&c);
    *max = *min;

    if (*c == ',') {
        ++c;
        *max = isdigit((unsigned char) *c) ? pattern_parse_count(&c) : -1;
    }

    if (*c != '}') return false;

    *cursor = c + 1;
    return true;
}

// Past a bracket expression, from the character after its '['. A class,
// equivalence class or collating element inside it ends with its own bracket.
// regcomp keeps the whole expression as one position.
static const char *pattern_skip_bracket(const char *cursor)
{
    if (*cursor == '^') ++cursor;
    if (*cursor == ']') ++cursor;

    while (*cursor && *cursor != ']') {
        if (cursor[0] == '[' && (cursor[1] == ':' || cursor[1] == '.' || cursor[1] == '=')) {
            char delimiter = cursor[1];
            cursor += 2;
            while (*cursor && !(cursor[0] == delimiter && cursor[1] == ']')) ++cursor;
            if (*cursor) cursor += 2;
        } else {
            ++cursor;
        }
    }

    return *cursor ? cursor + 1 : cursor;
}

// Ends the frame's current branch and adds it to its alternatives.
static void pattern_frame_alternate(struct pattern_frame *frame)
{
    struct pattern_cost branch = frame->has_branch ? frame->branch : pattern_cost_empty();
    frame->alternatives = frame->has_alternatives ? pattern_cost_union(frame->alternatives, branch) : branch;
    frame->has_alternatives = true;
    frame->has_branch = false;
}

static void pattern_frame_append(struct pattern_frame *frame, struct pattern_cost atom)
{
    frame->branch = frame->has_branch ? pattern_cost_concat(frame->branch, atom) : atom;
    frame->has_branch = true;
}

// The cost of compiling `pattern` as an extended regular expression, or
// INFINITY once it is certain to pass the limit. A syntax error is costed as
// far as it parses; regcomp refuses it afterwards.
static double pattern_cost(const char *pattern)
{
    struct pattern_frame frames[PATTERN_MAX_DEPTH + 1];
    int depth = 0;
    frames[0] = (struct pattern_frame) {0};

    for (const char *cursor = pattern; *cursor;) {
        struct pattern_cost atom;
        char c = *cursor++;

        if (c == '(') {
            if (depth == PATTERN_MAX_DEPTH) return INFINITY;
            frames[++depth] = (struct pattern_frame) {0};
            continue;
        } else if (c == '|') {
            pattern_frame_alternate(&frames[depth]);
            continue;
        } else if (c == ')' && depth > 0) {
            pattern_frame_alternate(&frames[depth]);
            atom = frames[depth--].alternatives;
        } else if (c == '[') {
            cursor = pattern_skip_bracket(cursor);
            atom = pattern_cost_position();
        } else {
            if (c == '\\' && *cursor) ++cursor;
            atom = pattern_cost_position();
        }

        int min, max;
        if (*cursor == '*') {
            ++cursor;
            atom = pattern_cost_repeat(atom, 0, -1);
        } else if (*cursor == '+') {
            ++cursor;
            atom = pattern_cost_repeat(atom, 1, -1);
        } else if (*cursor == '?') {
            ++cursor;
            atom = pattern_cost_repeat(atom, 0, 1);
        } else if (pattern_parse_bound(&cursor, &min, &max)) {
            atom = pattern_cost_repeat(atom, min, max);
        }

        pattern_frame_append(&frames[depth], atom);
        if (pattern_cost_total(frames[depth].branch) > PATTERN_COST_LIMIT) return INFINITY;
    }

    while (depth > 0) {
        pattern_frame_alternate(&frames[depth]);
        pattern_frame_append(&frames[depth - 1], frames[depth].alternatives);
        --depth;
    }

    pattern_frame_alternate(&frames[0]);
    return pattern_cost_total(frames[0].alternatives);
}

// Compiles a rule or signal pattern, or reports why not. regex_match never
// asks where a match is, so the pattern compiles without submatch tracking,
// which otherwise multiplies the cost of every optional part.
static bool pattern_compile(FILE *rsp, regex_t *regex, char *key, char *value)
{
    if (pattern_cost(value) > PATTERN_COST_LIMIT) {
        daemon_fail(rsp, "regex pattern for key '%s' is too complex\n", key);
        return false;
    }

    if (regcomp(regex, value, REG_EXTENDED | REG_NOSUB) != 0) {
        daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
        return false;
    }

    return true;
}
