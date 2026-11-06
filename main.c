#define _POSIX_C_SOURCE 200809L
#include "civetweb.h"
#include <ctype.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#define SIZE 9
#define CELLS 81
#define BODY_CAP 256

typedef enum { EASY, MEDIUM, HARD, RANDOM } Difficulty;
static volatile sig_atomic_t stopRequested = 0;
static const char *presets[] = {
    "530070000600195000098000060800060003400803001700020006060000280000419005000080079",
    "003020600900305001001806400008102900700000008006708200002609500800203009005010300",
    "000000907000420180000705026100904000050000040000507009920108000034059000507000000"
};

/* Record a shutdown request; the main loop stops CivetWeb cleanly afterward. */
static void requestStop(int signalNumber) { (void)signalNumber; stopRequested = 1; }

/* Return monotonic milliseconds so wall-clock adjustments cannot skew timings. */
static double nowMs(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t scale;
    if (scale.denom == 0) mach_timebase_info(&scale);
    return (double)mach_continuous_time() * (double)scale.numer / (double)scale.denom / 1000000.0;
#else
    struct timespec t;
    (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
#endif
}

/*
 * Check whether num can occupy one empty cell without breaking Sudoku rules.
 * grid is the current board; row and col identify the cell; num is 1..9.
 * Returns non-zero only when the number is legal at that position.
 */
static int isSafe(const int grid[SIZE][SIZE], int row, int col, int num) {
    int i, top, left;
    if (!grid || row < 0 || row >= SIZE || col < 0 || col >= SIZE || num < 1 || num > SIZE || grid[row][col] != 0) return 0;
    for (i = 0; i < SIZE; ++i) if (grid[row][i] == num || grid[i][col] == num) return 0;
    top = (row / 3) * 3; left = (col / 3) * 3;
    for (i = 0; i < 9; ++i) if (grid[top + i / 3][left + i % 3] == num) return 0;
    return 1;
}

/*
 * Validate the clues already present in a board, including its value range.
 * Returns zero for a duplicate in any row, column, or 3x3 box.
 */
static int isValidGrid(const int grid[SIZE][SIZE]) {
    int seen[10], r, c, n;
    if (!grid) return 0;
    for (r = 0; r < SIZE; ++r) for (c = 0; c < SIZE; ++c) if (grid[r][c] < 0 || grid[r][c] > 9) return 0;
    for (r = 0; r < SIZE; ++r) {
        memset(seen, 0, sizeof seen);
        for (c = 0; c < SIZE; ++c) if ((n = grid[r][c]) && seen[n]++) return 0;
    }
    for (c = 0; c < SIZE; ++c) {
        memset(seen, 0, sizeof seen);
        for (r = 0; r < SIZE; ++r) if ((n = grid[r][c]) && seen[n]++) return 0;
    }
    for (r = 0; r < SIZE; r += 3) for (c = 0; c < SIZE; c += 3) {
        int y, x;
        memset(seen, 0, sizeof seen);
        for (y = 0; y < 3; ++y) for (x = 0; x < 3; ++x) if ((n = grid[r + y][c + x]) && seen[n]++) return 0;
    }
    return 1;
}

/* Return true only when the board is full and all Sudoku constraints hold. */
static int isCompleteSolution(const int grid[SIZE][SIZE]) {
    int r, c;
    if (!isValidGrid(grid)) return 0;
    for (r = 0; r < SIZE; ++r) for (c = 0; c < SIZE; ++c) if (grid[r][c] == 0) return 0;
    return 1;
}

/*
 * Find the first zero-valued cell and write its coordinates through row/col.
 * Returns one when an empty cell is found, otherwise zero.
 */
static int findEmptyCell(const int grid[SIZE][SIZE], int *row, int *col) {
    int r, c;
    if (!grid || !row || !col) return 0;
    for (r = 0; r < SIZE; ++r) for (c = 0; c < SIZE; ++c) if (grid[r][c] == 0) { *row = r; *col = c; return 1; }
    return 0;
}

/*
 * Solve a valid partial grid in place using recursive backtracking.
 * attempts counts each candidate number considered across the full search.
 * Returns one on success; on failure, every speculative placement is undone.
 */
static int solveSudoku(int grid[SIZE][SIZE], unsigned long *attempts) {
    int emptyRow, emptyCol, candidate;
    if (!grid || !isValidGrid((const int (*)[SIZE])grid)) return 0;
    /* Solve the most direct next task: fill one empty cell before recursing. */
    if (!findEmptyCell((const int (*)[SIZE])grid, &emptyRow, &emptyCol)) return isCompleteSolution((const int (*)[SIZE])grid);
    for (candidate = 1; candidate <= SIZE; ++candidate) {
        /* Count each attempted digit, even if isSafe rejects it immediately. */
        if (attempts) ++*attempts;
        /* A candidate is placed only when its row, column, and box allow it. */
        if (isSafe((const int (*)[SIZE])grid, emptyRow, emptyCol, candidate)) {
            grid[emptyRow][emptyCol] = candidate; /* Make a temporary choice. */
            /* The recursive call tries to complete the rest of the board. */
            if (solveSudoku(grid, attempts)) return 1;
            /* That choice led to a dead end; restore empty before trying next. */
            grid[emptyRow][emptyCol] = 0;
        }
    }
    /* Every candidate failed, so this branch must report failure upward. */
    return 0;
}

/* Try candidates recursively until limit solutions have been found. */
/*
 * Explore one branch of the counting search.
 * limit is how many additional solutions this branch may still find; the
 * return value is the number actually found, capped at that limit.
 */
static int countRecursive(int grid[SIZE][SIZE], int limit) {
    int emptyRow, emptyCol, candidate, foundSolutions = 0;
    if (!findEmptyCell((const int (*)[SIZE])grid, &emptyRow, &emptyCol)) return 1;
    /* foundSolutions avoids exploring branches after the caller's limit. */
    for (candidate = 1; candidate <= SIZE && foundSolutions < limit; ++candidate) if (isSafe((const int (*)[SIZE])grid, emptyRow, emptyCol, candidate)) {
        grid[emptyRow][emptyCol] = candidate;
        foundSolutions += countRecursive(grid, limit - foundSolutions);
        grid[emptyRow][emptyCol] = 0;
    }
    return foundSolutions;
}

/*
 * Count solutions without changing the caller's grid. limit is normally 2:
 * zero means unsolvable, one means unique, and two means multiple solutions.
 */
static int countSolutions(const int input[SIZE][SIZE], int limit) {
    int copy[SIZE][SIZE], r;
    if (!input || limit < 1 || !isValidGrid(input)) return 0;
    for (r = 0; r < SIZE; ++r) memcpy(copy[r], input[r], sizeof copy[r]);
    return countRecursive(copy, limit);
}

/* Advance a small deterministic PRNG state used to shuffle valid patterns. */
static unsigned int nextRandom(unsigned int *state) { *state = *state * 1664525u + 1013904223u; return *state; }
/* Swap two integers; used by the in-place shuffle helper. */
static void swapInt(int *a, int *b) { int t = *a; *a = *b; *b = t; }
/* Fisher-Yates shuffle: each remaining position is paired with a random item. */
static void shuffle(int *items, int count, unsigned int *state) {
    int i;
    for (i = count - 1; i > 0; --i) swapInt(&items[i], &items[nextRandom(state) % (unsigned int)(i + 1)]);
}

/*
 * Generate a randomized, uniquely solvable puzzle in grid.
 * difficulty selects an approximate clue target; it is not a human rating.
 * Returns one only when the final board is valid and has exactly one solution.
 */
static int generatePuzzle(int grid[SIZE][SIZE], int difficulty) {
    int solved[SIZE][SIZE], map[10], rows[9], cols[9], positions[81];
    int bands[3] = {0, 1, 2}, stacks[3] = {0, 1, 2}, target, i, r, c;
    unsigned int state = (unsigned int)time(NULL) ^ (unsigned int)clock() ^ (unsigned int)(uintptr_t)grid;
    if (!grid || difficulty < EASY || difficulty > RANDOM) return 0;
    if (state == 0) state = 1;
    /* This cyclic pattern is a complete valid board before any shuffling. */
    for (r = 0; r < 9; ++r) for (c = 0; c < 9; ++c) solved[r][c] = (r * 3 + r / 3 + c) % 9 + 1;
    for (i = 0; i < 9; ++i) map[i + 1] = i + 1;
    shuffle(map + 1, 9, &state); shuffle(bands, 3, &state); shuffle(stacks, 3, &state);
    for (i = 0; i < 3; ++i) {
        int order[3] = {0, 1, 2}, k;
        shuffle(order, 3, &state);
        for (k = 0; k < 3; ++k) rows[i * 3 + k] = bands[i] * 3 + order[k];
        shuffle(order, 3, &state);
        for (k = 0; k < 3; ++k) cols[i * 3 + k] = stacks[i] * 3 + order[k];
    }
    /* Permute digits, bands, stacks, and rows/columns within each band/stack. */
    for (r = 0; r < 9; ++r) for (c = 0; c < 9; ++c) grid[r][c] = map[solved[rows[r]][cols[c]]];
    if (nextRandom(&state) & 1u) for (r = 0; r < 9; ++r) for (c = r + 1; c < 9; ++c) swapInt(&grid[r][c], &grid[c][r]);
    /* Targets are clue-count approximations, not human difficulty ratings. */
    target = difficulty == EASY ? 43 : difficulty == MEDIUM ? 37 : difficulty == HARD ? 31 : 39;
    for (i = 0; i < 81; ++i) positions[i] = i;
    shuffle(positions, 81, &state);
    for (i = 0; i < 81; ++i) {
        int p = positions[i], old, clues = 0, y, x;
        r = p / 9; c = p % 9; old = grid[r][c]; grid[r][c] = 0;
        /* Keep a removal only when at least one and at most one solution remains. */
        if (countSolutions((const int (*)[SIZE])grid, 2) != 1) grid[r][c] = old;
        for (y = 0; y < 9; ++y) for (x = 0; x < 9; ++x) clues += grid[y][x] != 0;
        if (clues <= target) break;
    }
    return isValidGrid((const int (*)[SIZE])grid) && countSolutions((const int (*)[SIZE])grid, 2) == 1;
}

/* Parse exactly 81 ASCII digits into a 9x9 grid; '0' denotes an empty cell. */
static int parseGrid(const char *text, size_t length, int grid[SIZE][SIZE]) {
    size_t i;
    if (!text || !grid || length != CELLS) return 0;
    for (i = 0; i < length; ++i) if (text[i] < '0' || text[i] > '9') return 0;
    for (i = 0; i < length; ++i) grid[i / 9][i % 9] = text[i] - '0';
    return 1;
}

/* Convert a grid to the same compact 81-digit representation used by the API. */
static void serializeGrid(const int grid[SIZE][SIZE], char out[CELLS + 1]) {
    int r, c;
    for (r = 0; r < 9; ++r) for (c = 0; c < 9; ++c) out[r * 9 + c] = (char)('0' + grid[r][c]);
    out[CELLS] = '\0';
}

/* Ensure every nonzero starting clue still has the same value in attempted. */
static int preservesClues(const int original[SIZE][SIZE], const int attempted[SIZE][SIZE]) {
    int r, c;
    for (r = 0; r < 9; ++r) for (c = 0; c < 9; ++c) if (original[r][c] && original[r][c] != attempted[r][c]) return 0;
    return 1;
}

/* Translate a supported lowercase difficulty name to its enum value. */
static int parseDifficulty(const char *text, size_t length, int allowRandom) {
    if (length == 4 && memcmp(text, "easy", 4) == 0) return EASY;
    if (length == 6 && memcmp(text, "medium", 6) == 0) return MEDIUM;
    if (length == 4 && memcmp(text, "hard", 4) == 0) return HARD;
    if (allowRandom && length == 6 && memcmp(text, "random", 6) == 0) return RANDOM;
    return -1;
}

/* Write a complete HTTP response segment, retrying short writes if needed. */
static int writeAll(struct mg_connection *connection, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) { int n = mg_write(connection, data + sent, length - sent); if (n <= 0) return 0; sent += (size_t)n; }
    return 1;
}

/* Send a JSON body with status, content type, and explicit content length. */
static int reply(struct mg_connection *connection, int status, const char *body) {
    char headers[220];
    const char *reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 404 ? "Not Found" : status == 413 ? "Payload Too Large" : status == 422 ? "Unprocessable Entity" : "Internal Server Error";
    int n = snprintf(headers, sizeof headers, "HTTP/1.1 %d %s\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status, reason, strlen(body));
    if (n < 0 || (size_t)n >= sizeof headers) return 500;
    (void)writeAll(connection, headers, (size_t)n); (void)writeAll(connection, body, strlen(body));
    return status; /* CivetWeb considers the custom request handled. */
}

/* Read a bounded text/plain POST body; -1 means too large, 0 means malformed. */
static int readBody(struct mg_connection *connection, const struct mg_request_info *info, char *body, size_t capacity, size_t *length) {
    long long expected = info->content_length;
    size_t received = 0;
    int i, plain = 0;
    for (i = 0; i < info->num_headers; ++i) if (strcasecmp(info->http_headers[i].name, "Content-Type") == 0 && strncasecmp(info->http_headers[i].value, "text/plain", 10) == 0) plain = 1;
    if (!plain || expected < 0) return 0;
    if ((unsigned long long)expected >= capacity) return -1;
    while (received < (size_t)expected) { int n = mg_read(connection, body + received, (size_t)expected - received); if (n <= 0) return 0; received += (size_t)n; }
    body[received] = '\0'; *length = received;
    return 1;
}

/* Route /api requests. Each branch parses data, calls engine functions, replies JSON. */
static int apiHandler(struct mg_connection *connection, void *userData) {
    const struct mg_request_info *info = mg_get_request_info(connection);
    const char *uri = info->local_uri;
    char body[BODY_CAP], output[512], puzzleText[82];
    size_t length = 0;
    int grid[9][9], originalGrid[9][9], userGrid[9][9], readResult;
    /* originalGrid is immutable clue input; userGrid is the submitted answer. */
    (void)userData;
    /* GET /api/health confirms that the local C service is responding. */
    /* GET /api/health reports that the loopback service is ready. */
    if (strcmp(uri, "/api/health") == 0) {
        if (strcmp(info->request_method, "GET") != 0) return reply(connection, 400, "{\"status\":\"error\",\"message\":\"GET required\"}");
        return reply(connection, 200, "{\"status\":\"ok\",\"service\":\"sudoku-studio-c\"}");
    }
    /* GET /api/puzzle returns one of the three curated C-side fixtures. */
    if (strcmp(uri, "/api/puzzle") == 0) {
        const char *q = info->query_string;
        int difficulty;
        if (strcmp(info->request_method, "GET") != 0 || !q || strncmp(q, "difficulty=", 11) != 0 || strchr(q, '&')) return reply(connection, 400, "{\"status\":\"error\",\"message\":\"Use ?difficulty=easy, medium, or hard\"}");
        difficulty = parseDifficulty(q + 11, strlen(q + 11), 0);
        if (difficulty < 0) return reply(connection, 400, "{\"status\":\"error\",\"message\":\"Unsupported difficulty\"}");
        (void)parseGrid(presets[difficulty], CELLS, grid); serializeGrid((const int (*)[9])grid, puzzleText);
        (void)snprintf(output, sizeof output, "{\"status\":\"ok\",\"difficulty\":\"%s\",\"puzzle\":\"%s\"}", difficulty == EASY ? "easy" : difficulty == MEDIUM ? "medium" : "hard", puzzleText);
        return reply(connection, 200, output);
    }
    if (strcmp(info->request_method, "POST") != 0) return reply(connection, 400, "{\"status\":\"error\",\"message\":\"POST required\"}");
    readResult = readBody(connection, info, body, sizeof body, &length);
    if (readResult == -1) return reply(connection, 413, "{\"status\":\"invalid_input\",\"message\":\"Payload is too large\"}");
    if (readResult == 0) return reply(connection, 400, "{\"status\":\"invalid_input\",\"message\":\"Expected a text/plain request body\"}");
    /* POST /api/validate parses an 81-cell custom grid and checks its clues. */
    if (strcmp(uri, "/api/validate") == 0) {
        if (!parseGrid(body, length, grid)) return reply(connection, 400, "{\"status\":\"invalid_input\",\"message\":\"Expected exactly 81 digits from 0 to 9\"}");
        if (!isValidGrid((const int (*)[9])grid)) return reply(connection, 422, "{\"status\":\"invalid_input\",\"message\":\"Clues conflict\"}");
        return reply(connection, 200, "{\"status\":\"valid\"}");
    }
    /* POST /api/solve times recursive search and returns its solved grid. */
    if (strcmp(uri, "/api/solve") == 0) {
        unsigned long attempts = 0; /* Candidate digits examined by recursion. */
        double start, elapsed;
        if (!parseGrid(body, length, grid)) return reply(connection, 400, "{\"status\":\"invalid_input\",\"message\":\"Expected exactly 81 digits from 0 to 9\"}");
        if (!isValidGrid((const int (*)[9])grid)) return reply(connection, 422, "{\"status\":\"invalid_input\",\"message\":\"Clues conflict\"}");
        memcpy(originalGrid, grid, sizeof grid); start = nowMs();
        if (!solveSudoku(grid, &attempts)) return reply(connection, 422, "{\"status\":\"unsolvable\",\"message\":\"This puzzle has no solution\"}");
        elapsed = nowMs() - start;
        if (!isCompleteSolution((const int (*)[9])grid) || !preservesClues((const int (*)[9])originalGrid, (const int (*)[9])grid)) return reply(connection, 500, "{\"status\":\"error\",\"message\":\"Solver produced an invalid result\"}");
        serializeGrid((const int (*)[9])grid, puzzleText);
        (void)snprintf(output, sizeof output, "{\"status\":\"solved\",\"solution\":\"%s\",\"candidate_attempts\":%lu,\"elapsed_ms\":%.3f}", puzzleText, attempts, elapsed);
        return reply(connection, 200, output);
    }
    /* POST /api/check receives original clues followed by the user's 81 cells. */
    if (strcmp(uri, "/api/check") == 0) {
        int r, c, complete = 1;
        if (length != 162 || !parseGrid(body, 81, originalGrid) || !parseGrid(body + 81, 81, userGrid)) return reply(connection, 400, "{\"status\":\"invalid_input\",\"message\":\"Expected 162 digits: puzzle then answer\"}");
        if (!isValidGrid((const int (*)[9])originalGrid)) return reply(connection, 200, "{\"status\":\"invalid_input\"}");
        for (r = 0; r < 9; ++r) for (c = 0; c < 9; ++c) if (userGrid[r][c] == 0) complete = 0;
        if (!complete) return reply(connection, 200, "{\"status\":\"incomplete\"}");
        if (!isCompleteSolution((const int (*)[9])userGrid) || !preservesClues((const int (*)[9])originalGrid, (const int (*)[9])userGrid)) return reply(connection, 200, "{\"status\":\"incorrect\"}");
        return reply(connection, 200, "{\"status\":\"correct\"}");
    }
    /* POST /api/generate makes a randomized puzzle at a requested profile. */
    if (strcmp(uri, "/api/generate") == 0) {
        int difficulty = parseDifficulty(body, length, 1);
        if (difficulty < 0) return reply(connection, 400, "{\"status\":\"error\",\"message\":\"Difficulty must be easy, medium, hard, or random\"}");
        if (!generatePuzzle(grid, difficulty)) return reply(connection, 422, "{\"status\":\"error\",\"message\":\"Could not generate a unique puzzle\"}");
        serializeGrid((const int (*)[9])grid, puzzleText);
        (void)snprintf(output, sizeof output, "{\"status\":\"ok\",\"difficulty\":\"%s\",\"puzzle\":\"%s\",\"unique_solution\":true}", body, puzzleText);
        return reply(connection, 200, output);
    }
    return reply(connection, 404, "{\"status\":\"error\",\"message\":\"Unknown API endpoint\"}");
}

/*
 * Find a valid-but-unsolvable variant of the unique Easy fixture for API tests.
 * The first safe-but-wrong value that leaves no solution becomes the fixture.
 */
static int makeUnsolvableFixture(char output[CELLS + 1]) {
    int grid[SIZE][SIZE], row, col, candidate;
    if (!parseGrid(presets[EASY], CELLS, grid)) return 0;
    for (row = 0; row < SIZE; ++row) for (col = 0; col < SIZE; ++col) if (grid[row][col] == 0) {
        for (candidate = 1; candidate <= SIZE; ++candidate) if (isSafe((const int (*)[SIZE])grid, row, col, candidate)) {
            grid[row][col] = candidate;
            if (countSolutions((const int (*)[SIZE])grid, 1) == 0) {
                serializeGrid((const int (*)[SIZE])grid, output);
                return 1;
            }
            grid[row][col] = 0;
        }
    }
    return 0;
}

/* Record one self-test result and return its pass state for aggregation. */
static int reportTest(const char *name, int passed) {
    printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    return passed;
}

/* Run focused C engine checks without introducing a second first-party C file. */
static int runSelfTests(void) {
    int passed = 0, total = 0, grid[SIZE][SIZE], solved[SIZE][SIZE];
    unsigned long attempts;
    char text[CELLS + 1];
    int preset, r, c;

#define CHECK_CASE(label, condition) do { ++total; passed += reportTest((label), (condition)); } while (0)
    for (preset = EASY; preset <= HARD; ++preset) {
        int loaded = parseGrid(presets[preset], CELLS, grid);
        CHECK_CASE(preset == EASY ? "easy preset is valid and unique" : preset == MEDIUM ? "medium preset is valid and unique" : "hard preset is valid and unique",
                   loaded && isValidGrid((const int (*)[SIZE])grid) && countSolutions((const int (*)[SIZE])grid, 2) == 1);
        if (loaded) {
            memcpy(solved, grid, sizeof grid); attempts = 0;
            CHECK_CASE("preset solves with recursive backtracking",
                       solveSudoku(solved, &attempts) && attempts > 0 && isCompleteSolution((const int (*)[SIZE])solved) && preservesClues((const int (*)[SIZE])grid, (const int (*)[SIZE])solved));
        }
    }

    memset(grid, 0, sizeof grid);
    grid[0][0] = 1; grid[0][1] = 1;
    CHECK_CASE("row duplicate is rejected", !isValidGrid((const int (*)[SIZE])grid));
    memset(grid, 0, sizeof grid);
    grid[0][0] = 1; grid[1][0] = 1;
    CHECK_CASE("column duplicate is rejected", !isValidGrid((const int (*)[SIZE])grid));
    memset(grid, 0, sizeof grid);
    grid[0][0] = 1; grid[1][1] = 1;
    CHECK_CASE("3x3 box duplicate is rejected", !isValidGrid((const int (*)[SIZE])grid));
    memset(grid, 0, sizeof grid);
    CHECK_CASE("incomplete grid is not a complete solution", !isCompleteSolution((const int (*)[SIZE])grid));
    CHECK_CASE("custom input accepts 81 empty cells", parseGrid("000000000000000000000000000000000000000000000000000000000000000000000000000000000", CELLS, grid));
    CHECK_CASE("short custom input is rejected", !parseGrid("000", 3, grid));
    CHECK_CASE("non-digit custom input is rejected", !parseGrid("00000000000000000000000000000000000000000000000000000000000000000000000000000000x", CELLS, grid));

    CHECK_CASE("modified original clue is detected", parseGrid(presets[EASY], CELLS, grid) && parseGrid(presets[EASY], CELLS, solved) && (solved[0][0] = 4, !preservesClues((const int (*)[SIZE])grid, (const int (*)[SIZE])solved)));
    CHECK_CASE("valid but unsolvable grid is detected", makeUnsolvableFixture(text) && parseGrid(text, CELLS, grid) && isValidGrid((const int (*)[SIZE])grid) && countSolutions((const int (*)[SIZE])grid, 2) == 0);
    CHECK_CASE("empty-cell search returns the first empty coordinate", (memset(grid, 0, sizeof grid), findEmptyCell((const int (*)[SIZE])grid, &r, &c) && r == 0 && c == 0));

    for (preset = EASY; preset <= RANDOM; ++preset) {
        int generated = generatePuzzle(grid, preset);
        CHECK_CASE(preset == EASY ? "easy generated puzzle is unique" : preset == MEDIUM ? "medium generated puzzle is unique" : preset == HARD ? "hard generated puzzle is unique" : "random generated puzzle is unique",
                   generated && isValidGrid((const int (*)[SIZE])grid) && countSolutions((const int (*)[SIZE])grid, 2) == 1);
    }
#undef CHECK_CASE
    printf("%d/%d C checks passed.\n", passed, total);
    return passed == total ? 0 : 1;
}

/* Forward CivetWeb diagnostics to stderr for local troubleshooting. */
static int logMessage(const struct mg_connection *connection, const char *message) { (void)connection; fprintf(stderr, "CivetWeb: %s\n", message); return 0; }

/* Configure a loopback-only HTTP server and serve the Vite output directory. */
int main(int argc, char **argv) {
    const char *root = ".";
    char listen[32];
    const char *options[7];
    struct mg_callbacks callbacks;
    struct mg_context *context;
    int port = 8080;
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) return runSelfTests();
    if (argc > 1 && strcmp(argv[1], "--print-unsolvable-fixture") == 0) {
        char fixture[CELLS + 1];
        if (!makeUnsolvableFixture(fixture)) return 1;
        puts(fixture);
        return 0;
    }
    if (argc > 1) { char *end = NULL; long parsed = strtol(argv[1], &end, 10); if (!end || *end || parsed < 1 || parsed > 65535) { fprintf(stderr, "Port must be between 1 and 65535.\n"); return 2; } port = (int)parsed; }
    if (argc > 2) root = argv[2];
    (void)snprintf(listen, sizeof listen, "127.0.0.1:%d", port);
    memset(&callbacks, 0, sizeof callbacks); callbacks.log_message = logMessage;
    options[0] = "listening_ports"; options[1] = listen; options[2] = "document_root"; options[3] = root;
    options[4] = "enable_directory_listing"; options[5] = "no"; options[6] = NULL;
    context = mg_start(&callbacks, NULL, options);
    if (!context) { fprintf(stderr, "Could not start server on %s. Check the port and document root.\n", listen); return 1; }
    mg_set_request_handler(context, "/api/", apiHandler, NULL);
    signal(SIGINT, requestStop); signal(SIGTERM, requestStop);
    printf("Sudoku Studio is running at http://%s\nPress Ctrl+C to stop.\n", listen); fflush(stdout);
    while (!stopRequested) { struct timespec pause = {0, 200000000}; (void)nanosleep(&pause, NULL); }
    mg_stop(context);
    return 0;
}
