// kdtree.h — Pure C23 3D KD-tree library
// Single-header: declare API, then #define KD_IMPLEMENTATION in one .c file.
// Used for 3D spatial queries on annotation points, skeleton nodes, mesh vertices.
#ifndef KDTREE_H
#define KDTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define KD_VERSION_MAJOR 0
#define KD_VERSION_MINOR 1
#define KD_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define KD_NODISCARD    [[nodiscard]]
  #define KD_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define KD_NODISCARD    __attribute__((warn_unused_result))
  #define KD_MAYBE_UNUSED __attribute__((unused))
#else
  #define KD_NODISCARD
  #define KD_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef KDDEF
  #ifdef KD_STATIC
    #define KDDEF static
  #else
    #define KDDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef KD_MALLOC
  #include <stdlib.h>
  #define KD_MALLOC(sz)       malloc(sz)
  #define KD_FREE(p)          free(p)
  #define KD_CALLOC(n, sz)    calloc(n, sz)
#endif

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum kd_status {
    KD_OK = 0,
    KD_ERR_NULL_ARG,
    KD_ERR_ALLOC,
    KD_ERR_EMPTY,
} kd_status;

// ── Types ───────────────────────────────────────────────────────────────────

typedef struct { double x, y, z; } kd_point;

typedef struct {
    int64_t  index;    // index into original points array
    double   dist;     // squared distance
} kd_result;

typedef struct kd_tree kd_tree;

// ── Build / Free ────────────────────────────────────────────────────────────

KD_NODISCARD KDDEF kd_status kd_build(kd_tree** out, const kd_point* points,
                                      int64_t n);
KDDEF void kd_free(kd_tree* tree);

// ── Queries ─────────────────────────────────────────────────────────────────

KD_NODISCARD KDDEF kd_status kd_nearest(const kd_tree* tree, kd_point query,
                                        kd_result* out);
KD_NODISCARD KDDEF kd_status kd_knn(const kd_tree* tree, kd_point query,
                                    int k, kd_result* results, int* found);
KD_NODISCARD KDDEF kd_status kd_radius(const kd_tree* tree, kd_point query,
                                       double radius, kd_result** results,
                                       int64_t* count);

// ── Metadata ────────────────────────────────────────────────────────────────

KDDEF int64_t  kd_size(const kd_tree* tree);
KDDEF kd_point kd_point_at(const kd_tree* tree, int64_t index);

// ── Utilities ───────────────────────────────────────────────────────────────

KDDEF const char* kd_status_str(kd_status s);
KDDEF const char* kd_version_str(void);

// ═══════════════════════════════════════════════════════════════════════════
// ██  IMPLEMENTATION  ██
// ═══════════════════════════════════════════════════════════════════════════

#ifdef KD_IMPLEMENTATION

#include <string.h>
#include <float.h>

// ── Internal Node ───────────────────────────────────────────────────────────

typedef struct {
    int8_t   axis;          // split axis: 0=x, 1=y, 2=z; -1 = leaf
    double   split;         // split value
    int64_t  left;          // left child index (-1 = none)
    int64_t  right;         // right child index (-1 = none)
    int64_t  point_index;   // index into original points array
} kd_node;

// ── Tree Structure ──────────────────────────────────────────────────────────

struct kd_tree {
    kd_node*  nodes;
    kd_point* points;       // copy of original points
    int64_t   n;            // number of points
    int64_t   node_count;   // number of nodes allocated
};

// ── Helpers ─────────────────────────────────────────────────────────────────

static double kd__dist_sq(kd_point a, kd_point b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return dx*dx + dy*dy + dz*dz;
}

static double kd__get_axis(kd_point p, int axis) {
    switch (axis) {
        case 0: return p.x;
        case 1: return p.y;
        case 2: return p.z;
        default: return 0.0;
    }
}

// ── Median Selection (nth_element via quickselect) ──────────────────────────

static void kd__swap_idx(int64_t* indices, int64_t a, int64_t b) {
    int64_t tmp = indices[a];
    indices[a] = indices[b];
    indices[b] = tmp;
}

static int64_t kd__partition(int64_t* indices, const kd_point* points,
                             int64_t lo, int64_t hi, int axis) {
    int64_t pivot_idx = indices[hi];
    double pivot_val = kd__get_axis(points[pivot_idx], axis);
    int64_t i = lo;
    for (int64_t j = lo; j < hi; j++) {
        if (kd__get_axis(points[indices[j]], axis) < pivot_val) {
            kd__swap_idx(indices, i, j);
            i++;
        }
    }
    kd__swap_idx(indices, i, hi);
    return i;
}

static void kd__nth_element(int64_t* indices, const kd_point* points,
                            int64_t lo, int64_t hi, int64_t nth, int axis) {
    while (lo < hi) {
        // Use median-of-three pivot for better partitioning
        int64_t mid = lo + (hi - lo) / 2;
        double lo_val  = kd__get_axis(points[indices[lo]], axis);
        double mid_val = kd__get_axis(points[indices[mid]], axis);
        double hi_val  = kd__get_axis(points[indices[hi]], axis);
        // Move median to hi position (pivot)
        if (lo_val <= mid_val) {
            if (mid_val <= hi_val) kd__swap_idx(indices, mid, hi);
            else if (lo_val <= hi_val) { /* hi already in place */ }
            else kd__swap_idx(indices, lo, hi);
        } else {
            if (lo_val <= hi_val) kd__swap_idx(indices, lo, hi);
            else if (mid_val <= hi_val) { /* hi already in place */ }
            else kd__swap_idx(indices, mid, hi);
        }

        int64_t p = kd__partition(indices, points, lo, hi, axis);
        if (p == nth) return;
        else if (nth < p) hi = p - 1;
        else lo = p + 1;
    }
}

// ── Recursive Build ─────────────────────────────────────────────────────────

static int64_t kd__build_rec(kd_node* nodes, int64_t* node_idx,
                             int64_t* indices, const kd_point* points,
                             int64_t lo, int64_t hi, int depth) {
    if (lo > hi) return -1;

    int axis = depth % 3;
    int64_t mid = lo + (hi - lo) / 2;

    kd__nth_element(indices, points, lo, hi, mid, axis);

    int64_t cur = (*node_idx)++;
    nodes[cur].axis = (int8_t)axis;
    nodes[cur].point_index = indices[mid];
    nodes[cur].split = kd__get_axis(points[indices[mid]], axis);

    nodes[cur].left  = kd__build_rec(nodes, node_idx, indices, points,
                                     lo, mid - 1, depth + 1);
    nodes[cur].right = kd__build_rec(nodes, node_idx, indices, points,
                                     mid + 1, hi, depth + 1);
    return cur;
}

// ── Build ───────────────────────────────────────────────────────────────────

KD_NODISCARD KDDEF kd_status kd_build(kd_tree** out, const kd_point* points,
                                      int64_t n) {
    if (!out) return KD_ERR_NULL_ARG;
    *out = NULL;
    if (n <= 0) return KD_ERR_EMPTY;
    if (!points) return KD_ERR_NULL_ARG;

    kd_tree* tree = (kd_tree*)KD_MALLOC(sizeof(kd_tree));
    if (!tree) return KD_ERR_ALLOC;

    tree->n = n;
    tree->node_count = n;
    tree->points = (kd_point*)KD_MALLOC((size_t)n * sizeof(kd_point));
    tree->nodes  = (kd_node*)KD_MALLOC((size_t)n * sizeof(kd_node));
    int64_t* indices = (int64_t*)KD_MALLOC((size_t)n * sizeof(int64_t));

    if (!tree->points || !tree->nodes || !indices) {
        if (tree->points) KD_FREE(tree->points);
        if (tree->nodes)  KD_FREE(tree->nodes);
        if (indices)       KD_FREE(indices);
        KD_FREE(tree);
        return KD_ERR_ALLOC;
    }

    memcpy(tree->points, points, (size_t)n * sizeof(kd_point));
    for (int64_t i = 0; i < n; i++) indices[i] = i;

    int64_t node_idx = 0;
    kd__build_rec(tree->nodes, &node_idx, indices, tree->points,
                  0, n - 1, 0);

    KD_FREE(indices);
    *out = tree;
    return KD_OK;
}

// ── Free ────────────────────────────────────────────────────────────────────

KDDEF void kd_free(kd_tree* tree) {
    if (!tree) return;
    if (tree->points) KD_FREE(tree->points);
    if (tree->nodes)  KD_FREE(tree->nodes);
    KD_FREE(tree);
}

// ── Nearest Neighbor ────────────────────────────────────────────────────────

static void kd__nearest_rec(const kd_tree* tree, int64_t node_idx,
                            kd_point query, int64_t* best_idx,
                            double* best_dist) {
    if (node_idx < 0) return;

    const kd_node* node = &tree->nodes[node_idx];
    double d = kd__dist_sq(query, tree->points[node->point_index]);
    if (d < *best_dist) {
        *best_dist = d;
        *best_idx = node->point_index;
    }

    double diff = kd__get_axis(query, node->axis) - node->split;
    int64_t first  = diff <= 0 ? node->left : node->right;
    int64_t second = diff <= 0 ? node->right : node->left;

    kd__nearest_rec(tree, first, query, best_idx, best_dist);
    if (diff * diff < *best_dist) {
        kd__nearest_rec(tree, second, query, best_idx, best_dist);
    }
}

KD_NODISCARD KDDEF kd_status kd_nearest(const kd_tree* tree, kd_point query,
                                        kd_result* out) {
    if (!tree || !out) return KD_ERR_NULL_ARG;

    int64_t best_idx = -1;
    double best_dist = DBL_MAX;
    kd__nearest_rec(tree, 0, query, &best_idx, &best_dist);

    out->index = best_idx;
    out->dist  = best_dist;
    return KD_OK;
}

// ── KNN (max-heap) ──────────────────────────────────────────────────────────

static void kd__heap_sift_down(kd_result* heap, int size, int i) {
    while (1) {
        int largest = i;
        int l = 2*i + 1;
        int r = 2*i + 2;
        if (l < size && heap[l].dist > heap[largest].dist) largest = l;
        if (r < size && heap[r].dist > heap[largest].dist) largest = r;
        if (largest == i) break;
        kd_result tmp = heap[i];
        heap[i] = heap[largest];
        heap[largest] = tmp;
        i = largest;
    }
}

static void kd__heap_sift_up(kd_result* heap, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap[i].dist <= heap[parent].dist) break;
        kd_result tmp = heap[i];
        heap[i] = heap[parent];
        heap[parent] = tmp;
        i = parent;
    }
}

static void kd__heap_insert(kd_result* heap, int* size, int k,
                            int64_t index, double dist) {
    if (*size < k) {
        heap[*size] = (kd_result){ .index = index, .dist = dist };
        (*size)++;
        kd__heap_sift_up(heap, *size - 1);
    } else if (dist < heap[0].dist) {
        heap[0] = (kd_result){ .index = index, .dist = dist };
        kd__heap_sift_down(heap, *size, 0);
    }
}

static void kd__knn_rec(const kd_tree* tree, int64_t node_idx,
                        kd_point query, kd_result* heap, int* size, int k) {
    if (node_idx < 0) return;

    const kd_node* node = &tree->nodes[node_idx];
    double d = kd__dist_sq(query, tree->points[node->point_index]);
    kd__heap_insert(heap, size, k, node->point_index, d);

    double diff = kd__get_axis(query, node->axis) - node->split;
    int64_t first  = diff <= 0 ? node->left : node->right;
    int64_t second = diff <= 0 ? node->right : node->left;

    kd__knn_rec(tree, first, query, heap, size, k);
    if (*size < k || diff * diff < heap[0].dist) {
        kd__knn_rec(tree, second, query, heap, size, k);
    }
}

KD_NODISCARD KDDEF kd_status kd_knn(const kd_tree* tree, kd_point query,
                                    int k, kd_result* results, int* found) {
    if (!tree || !results || !found) return KD_ERR_NULL_ARG;
    if (k <= 0) {
        *found = 0;
        return KD_OK;
    }

    int heap_size = 0;
    kd__knn_rec(tree, 0, query, results, &heap_size, k);

    // Sort results by distance (ascending) — simple insertion sort for small k
    for (int i = 1; i < heap_size; i++) {
        kd_result tmp = results[i];
        int j = i - 1;
        while (j >= 0 && results[j].dist > tmp.dist) {
            results[j+1] = results[j];
            j--;
        }
        results[j+1] = tmp;
    }

    *found = heap_size;
    return KD_OK;
}

// ── Radius Search ───────────────────────────────────────────────────────────

typedef struct {
    kd_result* data;
    int64_t    size;
    int64_t    cap;
} kd__result_vec;

static int kd__vec_push(kd__result_vec* v, int64_t index, double dist) {
    if (v->size >= v->cap) {
        int64_t new_cap = v->cap == 0 ? 16 : v->cap * 2;
        kd_result* new_data = (kd_result*)KD_MALLOC(
            (size_t)new_cap * sizeof(kd_result));
        if (!new_data) return -1;
        if (v->data) {
            memcpy(new_data, v->data, (size_t)v->size * sizeof(kd_result));
            KD_FREE(v->data);
        }
        v->data = new_data;
        v->cap = new_cap;
    }
    v->data[v->size++] = (kd_result){ .index = index, .dist = dist };
    return 0;
}

static int kd__radius_rec(const kd_tree* tree, int64_t node_idx,
                          kd_point query, double radius_sq,
                          kd__result_vec* results) {
    if (node_idx < 0) return 0;

    const kd_node* node = &tree->nodes[node_idx];
    double d = kd__dist_sq(query, tree->points[node->point_index]);
    if (d <= radius_sq) {
        if (kd__vec_push(results, node->point_index, d) < 0) return -1;
    }

    double diff = kd__get_axis(query, node->axis) - node->split;
    int64_t first  = diff <= 0 ? node->left : node->right;
    int64_t second = diff <= 0 ? node->right : node->left;

    if (kd__radius_rec(tree, first, query, radius_sq, results) < 0) return -1;
    if (diff * diff <= radius_sq) {
        if (kd__radius_rec(tree, second, query, radius_sq, results) < 0)
            return -1;
    }
    return 0;
}

KD_NODISCARD KDDEF kd_status kd_radius(const kd_tree* tree, kd_point query,
                                       double radius, kd_result** results,
                                       int64_t* count) {
    if (!tree || !results || !count) return KD_ERR_NULL_ARG;

    kd__result_vec vec = { .data = NULL, .size = 0, .cap = 0 };
    double radius_sq = radius * radius;

    if (kd__radius_rec(tree, 0, query, radius_sq, &vec) < 0) {
        if (vec.data) KD_FREE(vec.data);
        return KD_ERR_ALLOC;
    }

    // Sort by distance
    for (int64_t i = 1; i < vec.size; i++) {
        kd_result tmp = vec.data[i];
        int64_t j = i - 1;
        while (j >= 0 && vec.data[j].dist > tmp.dist) {
            vec.data[j+1] = vec.data[j];
            j--;
        }
        vec.data[j+1] = tmp;
    }

    *results = vec.data;
    *count = vec.size;
    return KD_OK;
}

// ── Metadata ────────────────────────────────────────────────────────────────

KDDEF int64_t kd_size(const kd_tree* tree) {
    return tree ? tree->n : 0;
}

KDDEF kd_point kd_point_at(const kd_tree* tree, int64_t index) {
    if (!tree || index < 0 || index >= tree->n)
        return (kd_point){ 0, 0, 0 };
    return tree->points[index];
}

// ── Utilities ───────────────────────────────────────────────────────────────

KDDEF const char* kd_status_str(kd_status s) {
    switch (s) {
        case KD_OK:           return "ok";
        case KD_ERR_NULL_ARG: return "null argument";
        case KD_ERR_ALLOC:    return "allocation failed";
        case KD_ERR_EMPTY:    return "empty input";
        default:              return "unknown";
    }
}

KDDEF const char* kd_version_str(void) {
    return "0.1.0";
}

#endif // KD_IMPLEMENTATION
#endif // KDTREE_H
