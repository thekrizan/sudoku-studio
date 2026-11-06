# C Backend Code Walkthrough

This guide follows the actual implementation in [`../main.c`](../main.c). The file contains the CivetWeb server, Sudoku engine, API routing, and a small self-test mode so the first-party backend remains in one C source file.

## 1. Grid representation

The board is a fixed-size two-dimensional array:

```c
int grid[9][9];
```

`grid[row][column]` addresses one cell. Values 1–9 are clues or entered digits; zero means the cell is empty. The HTTP API uses 81 characters in row-major order: the first nine characters form row zero, the next nine form row one, and so on. `parseGrid()` converts this compact string into the array, while `serializeGrid()` converts a result back into that format.

## 2. Validation functions

`isValidGrid()` walks the rows, columns, and 3×3 boxes. For each unit, a ten-element `seen` array records which values have already appeared. Zero is skipped because multiple empty cells are allowed. Values outside 0–9 are rejected before they can be used as indexes in `seen`.

`isSafe()` checks a single candidate number against its row and column, then visits the 3×3 box containing the cell. It requires the destination cell to be empty. This lets both the solver and solution counter reuse the same rule check.

`isCompleteSolution()` first requires a valid board, then confirms that no cell is zero. A full board with a repeated digit is therefore not accepted as a solution.

## 3. Finding the next empty cell

`findEmptyCell()` scans from the upper-left corner across each row. When it finds zero, it writes the location through the `row` and `col` pointers and returns one. If all cells contain digits, it returns zero.

The solver uses the first empty cell because this row-major strategy is simple and predictable. It does not use the more advanced “fewest candidates first” heuristic.

## 4. Recursive backtracking

`solveSudoku()` edits its `grid` argument in place. `attempts` points to a counter shared by every recursive call; it is incremented each time a candidate digit is considered.

```text
solveSudoku(grid):
    locate emptyRow, emptyCol
    if no empty cell exists:
        return complete-and-valid(grid)

    for candidate from 1 to 9:
        attempts += 1
        if isSafe(grid, emptyRow, emptyCol, candidate):
            grid[emptyRow][emptyCol] = candidate
            if solveSudoku(grid) succeeds:
                return success
            grid[emptyRow][emptyCol] = 0

    return failure
```

Why each step exists:

1. **Locate an empty cell:** there is nothing to solve after the grid is full, so this also identifies the base case.
2. **Try 1 through 9:** a valid Sudoku cell can contain only one of those digits.
3. **Check with `isSafe()`:** a conflicting value cannot be part of a solution and should not enter the search tree.
4. **Place a candidate temporarily:** the next recursive call needs to see the current choice while it solves the remaining cells.
5. **Recurse:** success means the rest of the grid can be completed from this choice. Success is returned through each parent call without removing the correct values.
6. **Reset after failure:** if recursion cannot complete the board, zero restores the cell to its empty state so the next candidate can be tried.
7. **Return failure after the loop:** every legal candidate has been considered and none completed the board, so the parent must reconsider its own choice.

The backend measures the call with `nowMs()` in `/api/solve`. The timer uses a monotonic clock so changes to the system clock do not affect the elapsed duration.

## 5. Counting solutions

`countSolutions()` copies the caller's grid to a local array, then asks `countRecursive()` to search that copy. The original remains unchanged. `limit` is the maximum number of solutions still needed. `foundSolutions` stores the total discovered so far in a recursive frame. Once that count reaches the limit, candidate exploration stops.

The generator passes a limit of two:

- Return 0: no solution exists.
- Return 1: exactly one solution exists.
- Return 2: at least two solutions exist, so the puzzle is ambiguous.

The exact number beyond two is not useful for checking uniqueness, so stopping early saves work.

## 6. Random generation

`generatePuzzle()` follows these stages:

1. Construct a complete valid cyclic Sudoku pattern in `solved`.
2. Shuffle a digit map, the order of row bands and column stacks, and row/column permutations inside each band or stack. These transformations keep the Latin row/column/box structure valid. It may also transpose the board.
3. Copy the transformed solution to the output grid.
4. Shuffle the 81 cell indexes. For each position, save its old value in `old` and tentatively put zero there.
5. Call `countSolutions(grid, 2)`. Keep the zero only if the result is exactly one. If the result is zero or two, restore `old` so the puzzle remains solvable and unique.
6. Stop near the clue target for the selected profile, then verify validity and uniqueness once more.

Easy, Medium, Hard, and Random use approximate clue targets. A target changes how many clues are removed; it does not measure whether a human would rate the puzzle easy or hard.

## 7. Presets and clue preservation

The three curated 81-digit strings are stored in the `presets` array at the top of `main.c`. `/api/puzzle` selects one based on its query parameter and parses it with `parseGrid()`.

`preservesClues(original, attempted)` walks both grids and rejects any change where the original cell was nonzero. The solver uses it as a final defensive check, and `/api/check` uses it to ensure a player's completed board retains the puzzle's givens.

## 8. HTTP request handling

`main()` configures CivetWeb with `listening_ports` set to `127.0.0.1:<port>`, sets the production document root, registers `apiHandler()` for `/api/`, and waits until a termination signal is received. CivetWeb serves files in the configured document root when a request is not handled as an API request.

`apiHandler()` gets the method, URI, query, and content length from CivetWeb. API POSTs must use `text/plain`. `readBody()` checks the content type and size before reading into its bounded buffer. `parseGrid()` then verifies the exact number and type of characters for grid payloads. `reply()` sends a JSON response with an HTTP status, content type, and content length.

| Route | Main operations |
| --- | --- |
| `GET /api/health` | Return a small service status response. |
| `GET /api/puzzle` | Parse a difficulty and load one of the stored presets. |
| `POST /api/generate` | Parse a profile and call `generatePuzzle()`. |
| `POST /api/validate` | Parse 81 cells and call `isValidGrid()`. |
| `POST /api/solve` | Validate, time `solveSudoku()`, verify the result, and return its metrics. |
| `POST /api/check` | Parse the original and user grids; report incomplete, incorrect, or correct. |

The C program has two additional command-line options used by local tests: `--self-test` runs engine checks, and `--print-unsolvable-fixture` prints a valid but unsolvable variant of the Easy puzzle for the HTTP test.

## 9. Reading order for a presentation

For a short code explanation, read the functions in this order:

1. `parseGrid()` and `serializeGrid()` — input/output representation.
2. `isValidGrid()`, `isSafe()`, and `isCompleteSolution()` — Sudoku constraints.
3. `findEmptyCell()` and `solveSudoku()` — recursion and backtracking.
4. `countRecursive()` and `countSolutions()` — bounded search for uniqueness.
5. `generatePuzzle()` — valid transformations and clue removal.
6. `apiHandler()` and `main()` — how browser requests reach the C engine.
